#include "UnityOpenVolumetricPlayer.h"

#include <cmath>
#include <utility>

namespace openvolumetric::unity
{

UnityOpenVolumetricPlayer::UnityOpenVolumetricPlayer(
	int id,
	std::unique_ptr<ITexture> texture,
	std::unique_ptr<IMeshBuffer> mesh_buffer)
	: m_id(id),
	  m_texture(std::move(texture)),
	  m_mesh_buffer(std::move(mesh_buffer))
{
}

UnityOpenVolumetricPlayer::~UnityOpenVolumetricPlayer()
{
	close();
}

bool UnityOpenVolumetricPlayer::open(const char* path)
{
	m_last_presented_time.store(-1.0, std::memory_order_release);
	m_centroid_sequence.store(0, std::memory_order_release);
	return m_player.open(path);
}

bool UnityOpenVolumetricPlayer::start()
{
	return m_player.start();
}

bool UnityOpenVolumetricPlayer::stop()
{
	m_player.stop();
	return true;
}

bool UnityOpenVolumetricPlayer::seek(double time)
{
	if (!m_player.seek(time))
		return false;
	m_last_presented_time.store(-1.0, std::memory_order_release);
	m_centroid_sequence.store(0, std::memory_order_release);
	return true;
}

void UnityOpenVolumetricPlayer::set_active_representation_id(
	const char* representation_id)
{
	m_player.set_active_representation_id(
		representation_id != nullptr ? representation_id : "");
}

AdaptiveSwitchInfo UnityOpenVolumetricPlayer::adaptive_switch_info() const
{
	return m_player.switch_info();
}

void UnityOpenVolumetricPlayer::clear_adaptive_policy()
{
	m_policy_representations.clear();
	m_adaptive_policy.configure({});
}

void UnityOpenVolumetricPlayer::add_adaptive_policy_representation(
	const char* id,
	const char* resource,
	std::uint64_t bandwidth)
{
	if (id == nullptr || resource == nullptr)
		return;
	m_policy_representations.push_back({id, resource, bandwidth});
	m_adaptive_policy.configure(m_policy_representations);
}

namespace
{

AdaptivePolicySwitchState policy_switch_state(AdaptiveSwitchState state)
{
	return static_cast<AdaptivePolicySwitchState>(state);
}

AdaptivePolicyInputState policy_input_state(ByteSourceState state)
{
	if (state == ByteSourceState::Ready || state == ByteSourceState::Ended)
		return AdaptivePolicyInputState::Ready;
	return static_cast<AdaptivePolicyInputState>(state);
}

} // namespace

AdaptivePolicyDecision UnityOpenVolumetricPlayer::update_adaptive_policy(
	const AdaptivePolicyObservation& supplied)
{
	AdaptivePolicyObservation observation = supplied;
	const AdaptiveSwitchInfo switch_info = m_player.switch_info();
	const OpenVolumetricBufferInfo buffer = m_player.buffer_info();
	observation.input_state = policy_input_state(buffer.state);
	observation.transfer_throughput_bps =
		buffer.transfer_throughput_bits_per_second;
	observation.downloaded_bytes = buffer.downloaded_bytes;
	observation.switch_state = policy_switch_state(switch_info.state);
	observation.switch_generation = switch_info.generation;
	observation.switch_count = switch_info.switch_count;
	observation.active_representation = switch_info.active_representation;
	AdaptivePolicyDecision decision = m_adaptive_policy.update(observation);
	if (decision.cancel_failed_switch)
		m_player.cancel_pending_switch();
	if (decision.action == AdaptivePolicyAction::Switch &&
		!m_player.request_switch(
			decision.target_resource,
			decision.target_representation,
			decision.boundary_time,
			decision.reason))
	{
		decision.action = AdaptivePolicyAction::RetryLater;
	}
	return decision;
}

AdaptivePolicyDecision UnityOpenVolumetricPlayer::request_adaptive_policy(
	std::size_t target_index,
	const AdaptivePolicyObservation& supplied)
{
	AdaptivePolicyObservation observation = supplied;
	const AdaptiveSwitchInfo switch_info = m_player.switch_info();
	observation.switch_state = policy_switch_state(switch_info.state);
	observation.switch_generation = switch_info.generation;
	observation.switch_count = switch_info.switch_count;
	observation.active_representation = switch_info.active_representation;
	AdaptivePolicyDecision decision =
		m_adaptive_policy.request(target_index, observation);
	if (decision.action == AdaptivePolicyAction::Switch &&
		!m_player.request_switch(
			decision.target_resource,
			decision.target_representation,
			decision.boundary_time,
			decision.reason))
	{
		decision.action = AdaptivePolicyAction::RetryLater;
	}
	return decision;
}

double UnityOpenVolumetricPlayer::adaptive_policy_throughput() const
{
	return m_adaptive_policy.smoothed_throughput_bps();
}

void UnityOpenVolumetricPlayer::close()
{
	m_player.close();
	if (m_texture)
		m_texture->destroy();
	if (m_mesh_buffer)
		m_mesh_buffer->destroy();
	m_last_presented_time.store(-1.0, std::memory_order_release);
	m_centroid_sequence.store(0, std::memory_order_release);
}

void UnityOpenVolumetricPlayer::set_presentation_time(double time)
{
	m_presentation_time.store(time, std::memory_order_release);
}

int UnityOpenVolumetricPlayer::render()
{
	OpenVolumetricPresentation presentation;
	const FrameMatchResult result = m_player.presentation(
		m_presentation_time.load(std::memory_order_acquire),
		presentation);
	if (result != FrameMatchResult::Ready)
		return -1;

	m_texture->upload(
		presentation.y.data(),
		presentation.u.data(),
		presentation.v.data());
	if (!m_mesh_buffer->update(&presentation.mesh))
		return -1;

	if (!presentation.mesh.verts.empty())
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		for (const Vertex& vertex : presentation.mesh.verts)
		{
			x += vertex.pos[0];
			y += vertex.pos[1];
			z += vertex.pos[2];
		}
		const double scale = 1.0 /
			static_cast<double>(presentation.mesh.verts.size());
		const std::uint64_t sequence =
			m_centroid_sequence.load(std::memory_order_relaxed);
		m_centroid_sequence.store(sequence + 1, std::memory_order_release);
		m_centroid_x.store(static_cast<float>(x * scale), std::memory_order_relaxed);
		m_centroid_y.store(static_cast<float>(y * scale), std::memory_order_relaxed);
		m_centroid_z.store(static_cast<float>(z * scale), std::memory_order_relaxed);
		m_centroid_sequence.store(sequence + 2, std::memory_order_release);
	}

	m_last_presented_time.store(
		presentation.presentation_time,
		std::memory_order_release);
	const double fps = m_player.media_info().frame_rate;
	return fps > 0.0
		? static_cast<int>(std::llround(
			presentation.presentation_time * fps))
		: 0;
}

const OpenVolumetricMediaInfo& UnityOpenVolumetricPlayer::media_info() const
{
	return m_player.media_info();
}

OpenVolumetricBufferInfo UnityOpenVolumetricPlayer::buffer_info() const
{
	return m_player.buffer_info();
}

OpenVolumetricAudioBufferInfo UnityOpenVolumetricPlayer::audio_buffer_info() const
{
	return m_player.audio_buffer_info();
}

std::string UnityOpenVolumetricPlayer::error() const
{
	return m_player.error();
}

int UnityOpenVolumetricPlayer::read_audio(float* output, int sample_count)
{
	return m_player.read_audio(output, sample_count);
}

double UnityOpenVolumetricPlayer::last_presented_time() const
{
	return m_last_presented_time.load(std::memory_order_acquire);
}

bool UnityOpenVolumetricPlayer::geometry_centroid(
	float& x,
	float& y,
	float& z) const
{
	for (int attempt = 0; attempt < 3; ++attempt)
	{
		const std::uint64_t before =
			m_centroid_sequence.load(std::memory_order_acquire);
		if (before == 0 || (before & 1) != 0)
			continue;
		const float current_x = m_centroid_x.load(std::memory_order_relaxed);
		const float current_y = m_centroid_y.load(std::memory_order_relaxed);
		const float current_z = m_centroid_z.load(std::memory_order_relaxed);
		const std::uint64_t after =
			m_centroid_sequence.load(std::memory_order_acquire);
		if (before == after)
		{
			x = current_x;
			y = current_y;
			z = current_z;
			return true;
		}
	}
	return false;
}

} // namespace openvolumetric::unity
