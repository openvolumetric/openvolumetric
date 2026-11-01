#include "OpenVolumetricFrameReader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace openvolumetric
{
namespace
{
using Clock = std::chrono::steady_clock;

std::int64_t timestamp_key(double time)
{
	constexpr double nanoseconds_per_second = 1000000000.0;
	return static_cast<std::int64_t>(
		std::llround(time * nanoseconds_per_second));
}

std::size_t presentation_bytes(const OpenVolumetricPresentation& presentation)
{
	return presentation.y.size() + presentation.u.size() +
		presentation.v.size() +
		(presentation.mesh.verts.size() * sizeof(Vertex)) +
		(presentation.mesh.indexes.size() * sizeof(int));
}
} // namespace

class OpenVolumetricFrameReader::Impl
{
public:
	explicit Impl(OpenVolumetricFrameReaderOptions requested_options)
		: options(requested_options)
	{
		if (options.request_timeout_milliseconds == 0)
			options.request_timeout_milliseconds = 1;
	}

	struct CacheEntry
	{
		std::int64_t key = 0;
		std::shared_ptr<const OpenVolumetricPresentation> presentation;
		std::size_t bytes = 0;
	};

	struct PendingRequest
	{
		FrameRequestResult result = FrameRequestResult::Failed;
		std::shared_ptr<const OpenVolumetricPresentation> presentation;
		std::string error;
		bool complete = false;
	};

	void clear_cache_locked()
	{
		cache.clear();
		cache_by_time.clear();
		cache_bytes = 0;
	}

	void cache_locked(
		std::int64_t key,
		const std::shared_ptr<const OpenVolumetricPresentation>& presentation)
	{
		const std::size_t bytes = presentation_bytes(*presentation);
		if (options.maximum_cached_presentations == 0 ||
			options.maximum_cache_bytes == 0 ||
			bytes > options.maximum_cache_bytes)
		{
			return;
		}

		cache.push_front({key, presentation, bytes});
		cache_by_time[key] = cache.begin();
		cache_bytes += bytes;
		while (cache.size() > options.maximum_cached_presentations ||
			cache_bytes > options.maximum_cache_bytes)
		{
			auto last = std::prev(cache.end());
			cache_bytes -= last->bytes;
			cache_by_time.erase(last->key);
			cache.erase(last);
			++counters.cache_evictions;
		}
	}

	OpenVolumetricFrameReaderOptions options;
	OpenVolumetricPlayer player;
	mutable std::mutex state_mutex;
	std::mutex decoder_mutex;
	std::condition_variable request_changed;
	std::list<CacheEntry> cache;
	std::unordered_map<std::int64_t, std::list<CacheEntry>::iterator>
		cache_by_time;
	std::unordered_map<std::int64_t, std::shared_ptr<PendingRequest>> pending;
	OpenVolumetricFrameReaderDiagnostics counters;
	OpenVolumetricMediaInfo info;
	std::string last_error;
	std::size_t cache_bytes = 0;
	std::uint64_t generation = 0;
	bool open = false;
};

OpenVolumetricFrameReader::OpenVolumetricFrameReader(
	OpenVolumetricFrameReaderOptions options)
	: m_impl(std::make_unique<Impl>(options))
{
}

OpenVolumetricFrameReader::~OpenVolumetricFrameReader()
{
	close();
}

bool OpenVolumetricFrameReader::open(const char* source)
{
	close();
	std::lock_guard<std::mutex> decoder_lock(m_impl->decoder_mutex);
	if (!m_impl->player.open(source) || !m_impl->player.start())
	{
		std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
		m_impl->last_error = m_impl->player.error().empty()
			? "The demand-driven OpenVolumetric decoder could not start."
			: m_impl->player.error();
		m_impl->player.close();
		return false;
	}

	std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
	m_impl->info = m_impl->player.media_info();
	m_impl->last_error.clear();
	m_impl->open = true;
	++m_impl->generation;
	return true;
}

void OpenVolumetricFrameReader::close()
{
	{
		std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
		m_impl->open = false;
		++m_impl->generation;
		for (auto& item : m_impl->pending)
		{
			item.second->result = FrameRequestResult::Cancelled;
			item.second->error = "The frame request was cancelled by shutdown.";
			item.second->complete = true;
		}
		m_impl->clear_cache_locked();
		m_impl->info = {};
	}
	m_impl->request_changed.notify_all();
	m_impl->player.cancel_pending_io();

	std::lock_guard<std::mutex> decoder_lock(m_impl->decoder_mutex);
	m_impl->player.close();
	std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
	m_impl->pending.clear();
}

void OpenVolumetricFrameReader::cancel_pending_requests()
{
	{
		std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
		++m_impl->generation;
		m_impl->pending.clear();
	}
	m_impl->request_changed.notify_all();
}

FrameRequestResult OpenVolumetricFrameReader::request(
	double presentation_time,
	std::shared_ptr<const OpenVolumetricPresentation>& output)
{
	output.reset();
	if (!std::isfinite(presentation_time) || presentation_time < 0.0)
		return FrameRequestResult::InvalidTime;

	const std::int64_t key = timestamp_key(presentation_time);
	std::shared_ptr<Impl::PendingRequest> pending;
	std::uint64_t generation = 0;
	bool leader = false;
	{
		std::unique_lock<std::mutex> state_lock(m_impl->state_mutex);
		if (!m_impl->open)
			return FrameRequestResult::NotOpen;

		auto cached = m_impl->cache_by_time.find(key);
		if (cached != m_impl->cache_by_time.end())
		{
			m_impl->cache.splice(
				m_impl->cache.begin(), m_impl->cache, cached->second);
			output = cached->second->presentation;
			++m_impl->counters.cache_hits;
			return FrameRequestResult::Ready;
		}

		auto existing = m_impl->pending.find(key);
		if (existing != m_impl->pending.end())
		{
			pending = existing->second;
			++m_impl->counters.coalesced_requests;
		}
		else
		{
			pending = std::make_shared<Impl::PendingRequest>();
			m_impl->pending.emplace(key, pending);
			++m_impl->counters.cache_misses;
			leader = true;
		}
		generation = m_impl->generation;

		if (!leader)
		{
			m_impl->request_changed.wait(state_lock, [&] {
				return pending->complete ||
					generation != m_impl->generation;
			});
			if (generation != m_impl->generation && !pending->complete)
				return FrameRequestResult::Cancelled;
			output = pending->presentation;
			if (!pending->error.empty())
				m_impl->last_error = pending->error;
			return pending->result;
		}
	}

	FrameRequestResult result = FrameRequestResult::Failed;
	std::shared_ptr<const OpenVolumetricPresentation> presentation;
	std::string request_error;
	{
		std::lock_guard<std::mutex> decoder_lock(m_impl->decoder_mutex);
		{
			std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
			if (!m_impl->open || generation != m_impl->generation)
				result = FrameRequestResult::Cancelled;
		}

		if (result != FrameRequestResult::Cancelled)
		{
			const auto deadline = Clock::now() + std::chrono::milliseconds(
				m_impl->options.request_timeout_milliseconds);
			const double frame_duration = m_impl->info.frame_rate > 0.0
				? 1.0 / m_impl->info.frame_rate
				: 1.0 / 30.0;
			double preroll = frame_duration;
			bool reached_start = presentation_time <= frame_duration;
			while (Clock::now() < deadline)
			{
				const double seek_time = reached_start
					? 0.0
					: std::max(0.0, presentation_time - preroll);
				reached_start = seek_time == 0.0;
				if (!m_impl->player.seek(seek_time))
				{
					request_error = m_impl->player.error();
					result = FrameRequestResult::Failed;
					break;
				}

				OpenVolumetricPresentation decoded;
				bool retry_with_more_preroll = false;
				while (Clock::now() < deadline)
				{
					{
						std::lock_guard<std::mutex> state_lock(
							m_impl->state_mutex);
						if (!m_impl->open ||
							generation != m_impl->generation)
						{
							result = FrameRequestResult::Cancelled;
							break;
						}
					}

					const FrameMatchResult match =
						m_impl->player.presentation(presentation_time, decoded);
					if (match == FrameMatchResult::Ready)
					{
						presentation =
							std::make_shared<OpenVolumetricPresentation>(
								std::move(decoded));
						result = FrameRequestResult::Ready;
						break;
					}
					if (match == FrameMatchResult::Missing)
					{
						// Temporal geometry may depend on a reference before
						// the nearest video access point. Widen the preroll
						// until the required reference has been reconstructed.
						retry_with_more_preroll = !reached_start;
						if (!retry_with_more_preroll)
						{
							request_error =
								"No complete texture and geometry presentation exists "
								"at the requested time.";
							result = FrameRequestResult::Missing;
						}
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				if (result == FrameRequestResult::Ready ||
					result == FrameRequestResult::Cancelled ||
					result == FrameRequestResult::Missing)
					break;
				if (!retry_with_more_preroll)
					break;
				preroll *= 2.0;
			}
			if (result == FrameRequestResult::Failed &&
				Clock::now() >= deadline)
			{
				request_error =
					"Timed out waiting for the requested presentation.";
				result = FrameRequestResult::Timeout;
			}
		}
	}

	{
		std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
		if (generation != m_impl->generation)
		{
			result = FrameRequestResult::Cancelled;
			presentation.reset();
			request_error = "The frame request became obsolete.";
		}
		else if (result == FrameRequestResult::Ready)
		{
			m_impl->cache_locked(key, presentation);
			m_impl->last_error.clear();
		}
		if (!request_error.empty())
			m_impl->last_error = request_error;
		pending->result = result;
		pending->presentation = presentation;
		pending->error = request_error;
		pending->complete = true;
		auto current = m_impl->pending.find(key);
		if (current != m_impl->pending.end() && current->second == pending)
			m_impl->pending.erase(current);
	}
	m_impl->request_changed.notify_all();
	output = std::move(presentation);
	return result;
}

OpenVolumetricMediaInfo OpenVolumetricFrameReader::media_info() const
{
	std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
	return m_impl->info;
}

OpenVolumetricFrameReaderDiagnostics
OpenVolumetricFrameReader::diagnostics() const
{
	std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
	auto result = m_impl->counters;
	result.cached_presentations = m_impl->cache.size();
	result.cached_bytes = m_impl->cache_bytes;
	return result;
}

std::string OpenVolumetricFrameReader::error() const
{
	std::lock_guard<std::mutex> state_lock(m_impl->state_mutex);
	return m_impl->last_error;
}

} // namespace openvolumetric
