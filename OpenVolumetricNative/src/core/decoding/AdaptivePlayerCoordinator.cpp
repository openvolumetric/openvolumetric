#include "AdaptivePlayerCoordinator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace openvolumetric
{

struct AdaptivePlayerCoordinator::Session
{
	OpenVolumetricPlayer player;
	std::string resource;
	std::string representation_id;
	OpenVolumetricPresentation primed_presentation;
	bool has_primed_presentation = false;
};

AdaptivePlayerCoordinator::AdaptivePlayerCoordinator() = default;

AdaptivePlayerCoordinator::~AdaptivePlayerCoordinator()
{
	close();
}

std::shared_ptr<AdaptivePlayerCoordinator::Session>
AdaptivePlayerCoordinator::active_session() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_active;
}

bool AdaptivePlayerCoordinator::open(
	const char* path,
	std::string representation_id)
{
	close();
	auto session = std::make_shared<Session>();
	if (path == nullptr || !session->player.open(path))
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_error = path == nullptr
			? "An OpenVolumetric MP4 path is required."
			: session->player.error();
		return false;
	}
	session->resource = path;
	session->representation_id = std::move(representation_id);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_active = std::move(session);
		m_switch_info = {};
		m_switch_info.active_representation =
			m_active->representation_id;
		m_media_info = m_active->player.media_info();
		m_error.clear();
	}
	return true;
}

bool AdaptivePlayerCoordinator::start()
{
	const auto active = active_session();
	if (!active)
		return false;
	if (!active->player.start())
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_error = active->player.error();
		return false;
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	m_started = true;
	return true;
}

void AdaptivePlayerCoordinator::stop()
{
	cancel_pending_switch();
	std::shared_ptr<Session> retired;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		retired = std::move(m_retired);
	}
	retired.reset();
	const auto active = active_session();
	if (active)
		active->player.stop();
	std::lock_guard<std::mutex> lock(m_mutex);
	m_started = false;
}

void AdaptivePlayerCoordinator::join_preparation_worker()
{
	if (m_preparation_worker.joinable() &&
		m_preparation_worker.get_id() != std::this_thread::get_id())
	{
		m_preparation_worker.join();
	}
}

void AdaptivePlayerCoordinator::cancel_pending_switch()
{
	std::shared_ptr<Session> preparing;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_switch_info.generation;
		preparing = m_preparing;
	}
	if (preparing)
		preparing->player.cancel_pending_io();
	join_preparation_worker();
	std::lock_guard<std::mutex> lock(m_mutex);
	m_preparing.reset();
	m_pending.reset();
	m_switch_info.state = AdaptiveSwitchState::Stable;
	m_switch_info.pending_representation.clear();
	m_switch_info.reason.clear();
	m_error.clear();
}

void AdaptivePlayerCoordinator::close()
{
	cancel_pending_switch();
	std::shared_ptr<Session> active;
	std::shared_ptr<Session> retired;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		active = std::move(m_active);
		retired = std::move(m_retired);
		m_started = false;
		m_switch_info = {};
		m_media_info = {};
		m_error.clear();
	}
	if (active)
		active->player.close();
	if (retired)
		retired->player.close();
}

bool AdaptivePlayerCoordinator::seek(double time)
{
	cancel_pending_switch();
	const auto active = active_session();
	if (!active || !active->player.seek(time))
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_error = active ? active->player.error() : "No active decoder session.";
		return false;
	}
	return true;
}

bool AdaptivePlayerCoordinator::request_switch(
	std::string resource,
	std::string representation_id,
	double boundary_time,
	std::string reason)
{
	if (resource.empty() || representation_id.empty() ||
		boundary_time < 0.0 || !std::isfinite(boundary_time))
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_error = "Adaptive switch resource, representation, or boundary is invalid.";
		return false;
	}
	cancel_pending_switch();
	std::shared_ptr<Session> retired;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		retired = std::move(m_retired);
	}
	retired.reset();
	const auto active = active_session();
	if (!active)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_error = "No active decoder session.";
		return false;
	}
	if (active->representation_id == representation_id)
		return true;

	auto candidate = std::make_shared<Session>();
	candidate->resource = std::move(resource);
	candidate->representation_id = std::move(representation_id);
	std::uint64_t generation = 0;
	bool should_start = false;
	const OpenVolumetricMediaInfo expected_info = media_info();
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		generation = ++m_switch_info.generation;
		should_start = m_started;
		m_preparing = candidate;
		m_switch_info.state = AdaptiveSwitchState::Preparing;
		m_switch_info.boundary_time = boundary_time;
		m_switch_info.pending_representation = candidate->representation_id;
		m_switch_info.reason = std::move(reason);
		m_error.clear();
	}

	m_preparation_worker = std::thread(
		[this, candidate, generation, boundary_time, should_start, expected_info]()
		{
			bool ready = candidate->player.open(candidate->resource.c_str());
			if (ready)
			{
				const OpenVolumetricMediaInfo& candidate_info =
					candidate->player.media_info();
				ready = candidate_info.width == expected_info.width &&
					candidate_info.height == expected_info.height &&
					std::abs(candidate_info.frame_rate - expected_info.frame_rate) < 0.001 &&
					std::abs(candidate_info.duration - expected_info.duration) < 0.05 &&
					candidate_info.has_audio == expected_info.has_audio &&
					candidate_info.audio_sample_rate == expected_info.audio_sample_rate &&
					candidate_info.audio_channels == expected_info.audio_channels;
			}
			if (ready)
				ready = candidate->player.seek(boundary_time);
			// Position the unopened worker before starting it. Starting at zero and
			// then seeking allowed FFmpeg to issue speculative reads from the start
			// of the representation, competing with the active stream for bandwidth.
			if (ready && should_start)
				ready = candidate->player.start();
			if (ready && should_start)
			{
				constexpr int maximum_attempts = 2000;
				for (int attempt = 0; attempt < maximum_attempts; ++attempt)
				{
					const FrameMatchResult result = candidate->player.presentation(
						boundary_time, candidate->primed_presentation);
					if (result == FrameMatchResult::Ready)
					{
						candidate->has_primed_presentation = true;
						break;
					}
					if (result == FrameMatchResult::Missing)
						break;
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				}
				ready = candidate->has_primed_presentation;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			if (generation != m_switch_info.generation)
				return;
			m_preparing.reset();
			if (!ready)
			{
				m_switch_info.state = AdaptiveSwitchState::Failed;
				m_error = candidate->player.error().empty()
					? "Adaptive representation could not preroll at the switch boundary."
					: candidate->player.error();
				return;
			}
			m_pending = candidate;
			m_switch_info.state = AdaptiveSwitchState::Ready;
		});
	return true;
}

void AdaptivePlayerCoordinator::set_active_representation_id(
	std::string representation_id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_active)
		m_active->representation_id = representation_id;
	m_switch_info.active_representation = std::move(representation_id);
}

bool AdaptivePlayerCoordinator::commit_if_ready(
	double requested_time,
	OpenVolumetricPresentation& output,
	bool& used_primed_presentation)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	used_primed_presentation = false;
	if (m_switch_info.state != AdaptiveSwitchState::Ready || !m_pending ||
		requested_time + 0.0001 < m_switch_info.boundary_time ||
		m_media_info.has_audio)
	{
		return false;
	}
	commit_pending_locked();
	if (m_active->has_primed_presentation)
	{
		output = std::move(m_active->primed_presentation);
		m_active->has_primed_presentation = false;
		used_primed_presentation = true;
	}
	return true;
}

bool AdaptivePlayerCoordinator::commit_pending_locked()
{
	if (m_switch_info.state != AdaptiveSwitchState::Ready || !m_pending)
		return false;
	m_retired = std::move(m_active);
	m_active = std::move(m_pending);
	m_switch_info.state = AdaptiveSwitchState::Stable;
	m_switch_info.active_representation = m_active->representation_id;
	m_switch_info.pending_representation.clear();
	++m_switch_info.switch_count;
	return true;
}

const OpenVolumetricMediaInfo& AdaptivePlayerCoordinator::media_info() const
{
	return m_media_info;
}

OpenVolumetricBufferInfo AdaptivePlayerCoordinator::buffer_info() const
{
	const auto active = active_session();
	return active ? active->player.buffer_info() : OpenVolumetricBufferInfo{};
}

OpenVolumetricAudioBufferInfo AdaptivePlayerCoordinator::audio_buffer_info() const
{
	const auto active = active_session();
	return active
		? active->player.audio_buffer_info()
		: OpenVolumetricAudioBufferInfo{};
}

std::string AdaptivePlayerCoordinator::error() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_error;
}

AdaptiveSwitchInfo AdaptivePlayerCoordinator::switch_info() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_switch_info;
}

FrameMatchResult AdaptivePlayerCoordinator::presentation(
	double requested_time,
	OpenVolumetricPresentation& output)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_media_info.has_audio &&
			m_switch_info.state == AdaptiveSwitchState::Ready &&
			requested_time > m_switch_info.boundary_time +
				(m_media_info.frame_rate > 0.0
					? 1.0 / m_media_info.frame_rate
					: 0.034))
		{
			m_switch_info.state = AdaptiveSwitchState::Failed;
			m_error = "Adaptive preparation missed its visual switch boundary.";
			return FrameMatchResult::NotReady;
		}
	}
	bool used_primed = false;
	commit_if_ready(requested_time, output, used_primed);
	if (used_primed)
		return FrameMatchResult::Ready;
	const auto active = active_session();
	if (!active)
		return FrameMatchResult::NotReady;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_media_info.has_audio &&
			m_switch_info.state == AdaptiveSwitchState::Ready &&
			requested_time + 0.0001 >= m_switch_info.boundary_time)
		{
			return FrameMatchResult::NotReady;
		}
	}
	if (active->has_primed_presentation &&
		requested_time + 0.0001 >=
			active->primed_presentation.presentation_time)
	{
		output = std::move(active->primed_presentation);
		active->has_primed_presentation = false;
		return FrameMatchResult::Ready;
	}
	return active
		? active->player.presentation(requested_time, output)
		: FrameMatchResult::NotReady;
}

int AdaptivePlayerCoordinator::read_audio(float* output, int sample_count)
{
	const auto active = active_session();
	if (!active || output == nullptr || sample_count <= 0)
		return 0;

	double boundary = 0.0;
	bool switch_ready = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		switch_ready = m_switch_info.state == AdaptiveSwitchState::Ready &&
			m_pending != nullptr;
		boundary = m_switch_info.boundary_time;
	}
	if (!switch_ready || !m_media_info.has_audio ||
		m_media_info.audio_sample_rate <= 0 ||
		m_media_info.audio_channels <= 0)
	{
		return active->player.read_audio(output, sample_count);
	}

	const OpenVolumetricAudioBufferInfo audio = active->player.audio_buffer_info();
	const double samples_per_second =
		static_cast<double>(m_media_info.audio_sample_rate) *
		static_cast<double>(m_media_info.audio_channels);
	const long long requested_before_boundary =
		std::llround((boundary - audio.read_time) * samples_per_second);
	if (requested_before_boundary < 0)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_switch_info.state == AdaptiveSwitchState::Ready)
		{
			m_switch_info.state = AdaptiveSwitchState::Failed;
			m_error = "Adaptive preparation missed its audio switch boundary.";
		}
		return active->player.read_audio(output, sample_count);
	}
	if (requested_before_boundary > sample_count)
		return active->player.read_audio(output, sample_count);
	const int samples_before_boundary = static_cast<int>(std::max(
		0LL, requested_before_boundary));
	const int first = active->player.read_audio(output, samples_before_boundary);
	if (first < samples_before_boundary)
		return first;

	std::shared_ptr<Session> switched;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (commit_pending_locked())
			switched = m_active;
	}
	if (!switched)
		return first;
	return first + switched->player.read_audio(
		output + first, sample_count - first);
}

} // namespace openvolumetric
