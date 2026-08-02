#include "HttpRangeByteSource.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <utility>

namespace openvolumetric
{
namespace
{

std::once_flag g_curl_initialization;
CURLcode g_curl_initialization_result = CURLE_FAILED_INIT;

void initialize_curl()
{
	g_curl_initialization_result = curl_global_init(CURL_GLOBAL_DEFAULT);
}

struct DownloadBuffer
{
	std::vector<std::uint8_t> bytes;
	std::size_t maximum_size = 0;
	bool overflow = false;
};

std::size_t write_download(
	char* data,
	std::size_t item_size,
	std::size_t item_count,
	void* user_data)
{
	auto* output = static_cast<DownloadBuffer*>(user_data);
	if (item_count != 0 &&
		item_size > std::numeric_limits<std::size_t>::max() / item_count)
	{
		if (output != nullptr)
			output->overflow = true;
		return 0;
	}
	const std::size_t size = item_size * item_count;
	if (output == nullptr || data == nullptr ||
		output->bytes.size() > output->maximum_size ||
		size > output->maximum_size - output->bytes.size())
	{
		if (output != nullptr)
			output->overflow = true;
		return 0;
	}
	const auto* begin = reinterpret_cast<const std::uint8_t*>(data);
	output->bytes.insert(output->bytes.end(), begin, begin + size);
	return size;
}

int transfer_progress(
	void* user_data,
	curl_off_t,
	curl_off_t,
	curl_off_t,
	curl_off_t)
{
	const auto* cancelled = static_cast<const std::atomic<bool>*>(user_data);
	return cancelled != nullptr &&
		cancelled->load(std::memory_order_acquire)
		? 1
		: 0;
}

void configure_request(
	CURL* handle,
	const std::string& url,
	const HttpRangeByteSourceOptions& options,
	std::atomic<bool>* cancelled)
{
	curl_easy_reset(handle);
	curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
	curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, options.connection_timeout_ms);
	curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, options.request_timeout_ms);
	curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, 5L);
	curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "identity");
	curl_easy_setopt(handle, CURLOPT_USERAGENT, "OpenVolumetric/0.1");
#if defined(__ANDROID__)
	// vcpkg's OpenSSL build has no packaged CA bundle. Android exposes its
	// trusted roots as a hashed certificate directory for native clients.
	curl_easy_setopt(
		handle,
		CURLOPT_CAPATH,
		"/system/etc/security/cacerts");
#endif
	curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &transfer_progress);
	curl_easy_setopt(handle, CURLOPT_XFERINFODATA, cancelled);
}

std::string curl_failure(CURLcode result)
{
	return curl_easy_strerror(result);
}

/// Returns whether repeating a request can recover from the curl failure.
///
/// HTTP status is deliberately not considered here. libcurl can retain a
/// valid response code from an interrupted transfer, so coupling a transport
/// error to that code incorrectly made a brief Wi-Fi outage terminal.
bool is_retryable_transport_failure(CURLcode result)
{
	switch (result)
	{
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_CONNECT:
	case CURLE_PARTIAL_FILE:
	case CURLE_HTTP2:
	case CURLE_WRITE_ERROR:
	case CURLE_UPLOAD_FAILED:
	case CURLE_READ_ERROR:
	case CURLE_OPERATION_TIMEDOUT:
	case CURLE_SEND_ERROR:
	case CURLE_RECV_ERROR:
	case CURLE_GOT_NOTHING:
	case CURLE_HTTP2_STREAM:
		return true;
	default:
		return false;
	}
}

} // namespace

HttpRangeByteSource::HttpRangeByteSource(
	std::string url,
	HttpRangeByteSourceOptions options)
	: m_url(std::move(url)),
	  m_options(options)
{
	if (m_url.empty())
	{
		m_error = "HTTP byte source requires a URL.";
		m_worker_stopped = true;
		return;
	}
	if (m_options.block_size == 0 ||
		m_options.maximum_cache_bytes < m_options.block_size ||
		m_options.fragment_read_ahead_count == 0 ||
		m_options.sequential_read_ahead_blocks >
			m_options.maximum_cache_bytes / m_options.block_size ||
		m_options.connection_timeout_ms <= 0 ||
		m_options.request_timeout_ms <= 0 ||
		m_options.maximum_retry_count < 0 ||
		m_options.initial_retry_delay_ms <= 0 ||
		m_options.maximum_retry_delay_ms < m_options.initial_retry_delay_ms)
	{
		m_error = "HTTP byte-source cache and timeout settings are invalid.";
		m_worker_stopped = true;
		return;
	}

	m_requested_block = 0;
	m_worker = std::thread(&HttpRangeByteSource::worker_loop, this);
	std::unique_lock<std::mutex> lock(m_mutex);
	m_condition.wait(lock, [&]()
	{
		return m_ready || m_worker_stopped || !m_error.empty();
	});
}

HttpRangeByteSource::~HttpRangeByteSource()
{
	cancel();
	if (m_worker.joinable())
		m_worker.join();
}

std::int64_t HttpRangeByteSource::read(
	std::uint8_t* destination,
	std::size_t capacity)
{
	if (destination == nullptr)
		return -1;
	if (capacity == 0)
		return 0;

	std::size_t copied = 0;
	while (copied < capacity)
	{
		std::uint64_t block_index = 0;
		std::size_t block_offset = 0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_error.empty() || is_cancelled() ||
				m_size < 0 || m_position < 0)
				return -1;
			if (m_position >= m_size)
			{
				m_state = ByteSourceState::Ended;
				break;
			}
			block_index = static_cast<std::uint64_t>(
				m_position / static_cast<std::int64_t>(m_options.block_size));
			block_offset = static_cast<std::size_t>(
				m_position % static_cast<std::int64_t>(m_options.block_size));
		}
		if (!wait_for_block(block_index))
		{
			// Never report a prefix of a failed logical read as success.
			// FFmpeg can interpret such a prefix as a complete compressed
			// packet and surface transport truncation as codec corruption.
			return -1;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		auto iterator = m_cache.find(block_index);
		if (iterator == m_cache.end() ||
			block_offset >= iterator->second.bytes.size())
			break;
		iterator->second.last_use = ++m_use_counter;
		const std::size_t available =
			iterator->second.bytes.size() - block_offset;
		const std::size_t count = std::min(capacity - copied, available);
		std::memcpy(
			destination + copied,
			iterator->second.bytes.data() + block_offset,
			count);
		copied += count;
		m_position += static_cast<std::int64_t>(count);
	}
	return static_cast<std::int64_t>(copied);
}

std::int64_t HttpRangeByteSource::seek(
	std::int64_t offset,
	ByteSeekOrigin origin)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_ready || is_cancelled() || !m_error.empty())
		return -1;

	std::int64_t base = 0;
	switch (origin)
	{
	case ByteSeekOrigin::Begin:
		break;
	case ByteSeekOrigin::Current:
		base = m_position;
		break;
	case ByteSeekOrigin::End:
		base = m_size;
		break;
	}
	if ((offset > 0 &&
			base > std::numeric_limits<std::int64_t>::max() - offset) ||
		(offset < 0 &&
			base < std::numeric_limits<std::int64_t>::min() - offset))
		return -1;
	const std::int64_t target = base + offset;
	if (target < 0 || target > m_size)
		return -1;
	m_position = target;
	if (m_state == ByteSourceState::Ended)
		m_state = ByteSourceState::Ready;
	return m_position;
}

std::int64_t HttpRangeByteSource::size() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_size;
}

bool HttpRangeByteSource::is_seekable() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_ready && m_size >= 0 && m_error.empty();
}

void HttpRangeByteSource::cancel()
{
	m_cancelled.store(true, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_state = ByteSourceState::Cancelled;
	}
	m_condition.notify_all();
}

bool HttpRangeByteSource::is_cancelled() const
{
	return m_cancelled.load(std::memory_order_acquire);
}

const std::string& HttpRangeByteSource::error() const
{
	return m_error;
}

ByteSourceDiagnostics HttpRangeByteSource::diagnostics() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	ByteSourceDiagnostics result;
	result.state = m_state;
	result.remote = true;
	result.resource_size_bytes = m_size;
	result.cached_bytes = m_cache_bytes;
	result.downloaded_bytes = m_downloaded_bytes;
	result.transfer_throughput_bits_per_second = static_cast<std::uint64_t>(
		m_transfer_throughput_bits_per_second);
	result.request_count = m_request_count;
	result.recovery_count = m_recovery_count;
	result.fragmented = !m_fragments.empty();
	result.fragment_count = m_fragments.size();
	result.active_fragment = m_active_fragment.has_value()
		? static_cast<std::int64_t>(*m_active_fragment)
		: -1;
	result.cached_fragment_count = cached_fragment_count();
	return result;
}

bool HttpRangeByteSource::is_open() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_ready && m_error.empty();
}

void HttpRangeByteSource::enable_fragment_prefetch()
{
	m_fragment_prefetch_enabled.store(true, std::memory_order_release);
}

void HttpRangeByteSource::worker_loop()
{
	std::call_once(g_curl_initialization, &initialize_curl);
	if (g_curl_initialization_result != CURLE_OK)
	{
		fail("libcurl global initialization failed.");
		return;
	}

	CURL* handle = curl_easy_init();
	if (handle == nullptr)
	{
		fail("Could not create the HTTP transfer handle.");
		return;
	}
	if (!discover_resource(handle))
	{
		curl_easy_cleanup(handle);
		return;
	}
	discover_fragment_index(handle);

	while (!is_cancelled())
	{
		std::uint64_t block_index = 0;
		bool block_is_cached = false;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_condition.wait(lock, [&]()
			{
				return is_cancelled() || m_requested_block.has_value();
			});
			if (is_cancelled())
				break;
			block_index = *m_requested_block;
			m_requested_block.reset();
			block_is_cached = m_cache.find(block_index) != m_cache.end();
		}
		if (!block_is_cached && !download_block(handle, block_index))
			break;

		if (!m_fragments.empty())
		{
			if (m_fragment_prefetch_enabled.load(std::memory_order_acquire))
				prefetch_fragment_window(handle, block_index);
			continue;
		}

		// Conventional MP4 input retains bounded sequential read-ahead. A new
		// explicit demux request always supersedes speculative downloads.
		for (std::size_t read_ahead = 0;
			read_ahead < m_options.sequential_read_ahead_blocks &&
			!is_cancelled();
			++read_ahead)
		{
			const std::uint64_t next_block = block_index + 1;
			bool should_download = false;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_requested_block.has_value())
					break;
				const std::uint64_t start =
					next_block * m_options.block_size;
				if (m_size < 0 ||
					start >= static_cast<std::uint64_t>(m_size))
					break;
				should_download =
					m_cache.find(next_block) == m_cache.end();
			}
			block_index = next_block;
			if (should_download && !download_block(handle, block_index))
				return;
		}
	}

	curl_easy_cleanup(handle);
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_worker_stopped = true;
	}
	m_condition.notify_all();
}

bool HttpRangeByteSource::discover_resource(void* raw_handle)
{
	auto* handle = static_cast<CURL*>(raw_handle);
	configure_request(handle, m_url, m_options, &m_cancelled);
	curl_easy_setopt(handle, CURLOPT_NOBODY, 1L);
	const CURLcode result = curl_easy_perform(handle);
	if (result != CURLE_OK)
	{
		fail("HTTP metadata request failed: " + curl_failure(result));
		return false;
	}

	long status = 0;
	curl_off_t content_length = -1;
	curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_getinfo(
		handle,
		CURLINFO_CONTENT_LENGTH_DOWNLOAD_T,
		&content_length);
	if (status < 200 || status >= 400 || content_length <= 0)
	{
		fail("HTTP resource did not provide a valid content length.");
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_size = static_cast<std::int64_t>(content_length);
	}
	return download_block(handle, 0);
}

void HttpRangeByteSource::discover_fragment_index(void* handle)
{
	std::uint64_t last_block = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_size <= 0 || m_cache.empty())
			return;
		const auto& first = m_cache.begin()->second.bytes;
		constexpr std::uint8_t mvex[] = {'m', 'v', 'e', 'x'};
		const bool fragmented = std::search(
			first.begin(), first.end(),
			std::begin(mvex), std::end(mvex)) != first.end();
		if (!fragmented)
			return;
		last_block = static_cast<std::uint64_t>(m_size - 1) /
			m_options.block_size;
	}
	if (last_block != 0 && !download_block(handle, last_block))
		return;

	std::lock_guard<std::mutex> lock(m_mutex);
	const auto iterator = m_cache.find(last_block);
	if (iterator == m_cache.end())
		return;
	const std::uint64_t tail_offset = last_block * m_options.block_size;
	m_fragments = parse_fragmented_mp4_index(
		iterator->second.bytes.data(),
		iterator->second.bytes.size(),
		tail_offset,
		static_cast<std::uint64_t>(m_size));
	if (!m_fragments.empty())
	{
		// Schedule the first fragment after index discovery. The initialization
		// block is normally already cached; the worker still uses it to select
		// and fill the first bounded fragment window in the background.
		if (!m_requested_block.has_value())
		{
			m_requested_block =
				m_fragments.front().offset / m_options.block_size;
		}
		m_condition.notify_all();
	}
}

std::optional<std::size_t> HttpRangeByteSource::fragment_for_block(
	std::uint64_t block_index) const
{
	const std::uint64_t offset = block_index * m_options.block_size;
	for (std::size_t index = 0; index < m_fragments.size(); ++index)
	{
		const Mp4FragmentRange& fragment = m_fragments[index];
		if (offset < fragment.offset + fragment.size &&
			offset + m_options.block_size > fragment.offset)
			return index;
	}
	return std::nullopt;
}

void HttpRangeByteSource::prefetch_fragment_window(
	void* handle,
	std::uint64_t demanded_block)
{
	const std::optional<std::size_t> active = fragment_for_block(demanded_block);
	if (!active.has_value())
		return;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_active_fragment = active;
	}

	std::uint64_t scheduled_bytes = 0;
	for (std::size_t index = *active; index < m_fragments.size(); ++index)
	{
		if (index - *active >= m_options.fragment_read_ahead_count)
			break;
		const Mp4FragmentRange& fragment = m_fragments[index];
		if (index != *active &&
			scheduled_bytes + fragment.size > m_options.maximum_cache_bytes)
			break;
		scheduled_bytes += fragment.size;
		const std::uint64_t first_block = fragment.offset /
			m_options.block_size;
		const std::uint64_t last_block =
			(fragment.offset + fragment.size - 1) / m_options.block_size;
		for (std::uint64_t block = first_block; block <= last_block; ++block)
		{
			bool missing = false;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (m_requested_block.has_value())
					return;
				missing = m_cache.find(block) == m_cache.end();
			}
			if (missing && !download_block(handle, block))
				return;
		}
	}
}

std::uint64_t HttpRangeByteSource::cached_fragment_count() const
{
	std::uint64_t count = 0;
	for (const Mp4FragmentRange& fragment : m_fragments)
	{
		const std::uint64_t first = fragment.offset / m_options.block_size;
		const std::uint64_t last =
			(fragment.offset + fragment.size - 1) / m_options.block_size;
		bool complete = true;
		for (std::uint64_t block = first; block <= last; ++block)
		{
			if (m_cache.find(block) == m_cache.end())
			{
				complete = false;
				break;
			}
		}
		if (complete)
			++count;
	}
	return count;
}

bool HttpRangeByteSource::download_block(
	void* raw_handle,
	std::uint64_t block_index)
{
	auto* handle = static_cast<CURL*>(raw_handle);
	const std::uint64_t start = block_index * m_options.block_size;
	std::int64_t resource_size = 0;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		resource_size = m_size;
	}
	if (start >= static_cast<std::uint64_t>(resource_size))
	{
		fail("HTTP range begins beyond the resource.");
		return false;
	}
	const std::uint64_t end = std::min<std::uint64_t>(
		start + m_options.block_size - 1,
		static_cast<std::uint64_t>(resource_size - 1));

	const std::string range =
		std::to_string(start) + "-" + std::to_string(end);
	long retry_delay = m_options.initial_retry_delay_ms;
	for (int attempt = 0;
		attempt <= m_options.maximum_retry_count && !is_cancelled();
		++attempt)
	{
		configure_request(handle, m_url, m_options, &m_cancelled);
		DownloadBuffer output;
		output.maximum_size = static_cast<std::size_t>(end - start + 1);
		output.bytes.reserve(output.maximum_size);
		curl_easy_setopt(handle, CURLOPT_RANGE, range.c_str());
		curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &write_download);
		curl_easy_setopt(handle, CURLOPT_WRITEDATA, &output);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_request_count;
		}
		const auto transfer_started = std::chrono::steady_clock::now();
		const CURLcode result = curl_easy_perform(handle);
		const double transfer_seconds = std::chrono::duration<double>(
			std::chrono::steady_clock::now() - transfer_started).count();

		long status = 0;
		curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
		const bool whole_small_resource =
			status == 200 &&
			static_cast<std::uint64_t>(resource_size) <= m_options.block_size;
		const bool valid_status = status == 206 || whole_small_resource;
		const bool complete =
			output.bytes.size() == output.maximum_size;
		if (result == CURLE_OK && valid_status && complete)
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_downloaded_bytes += output.bytes.size();
				if (transfer_seconds > 0.0)
				{
					const double measured =
						static_cast<double>(output.bytes.size()) * 8.0 /
						transfer_seconds;
					m_transfer_throughput_bits_per_second =
						m_transfer_throughput_bits_per_second <= 0.0
						? measured
						: 0.75 * m_transfer_throughput_bits_per_second +
							0.25 * measured;
				}
			}
			insert_block(block_index, std::move(output.bytes));
			return true;
		}

		if (output.overflow)
		{
			fail("HTTP server ignored or exceeded the requested byte range.");
			return false;
		}
		const bool retryable_status =
			status == 0 || status == 408 || status == 429 ||
			(status >= 500 && status < 600);
		const bool retryable_failure =
			(result != CURLE_OK &&
				is_retryable_transport_failure(result)) ||
			(result == CURLE_OK && retryable_status) ||
			(result == CURLE_OK && valid_status && !complete);
		if (attempt >= m_options.maximum_retry_count ||
			!retryable_failure ||
			result == CURLE_ABORTED_BY_CALLBACK)
		{
			if (result != CURLE_OK)
			{
				fail("HTTP range request failed after retries: " +
					curl_failure(result));
			}
			else if (!valid_status)
			{
				fail("HTTP server does not support bounded byte-range requests.");
			}
			else
			{
				fail("HTTP range response remained truncated after retries.");
			}
			return false;
		}

		set_state(ByteSourceState::Rebuffering);
		if (!wait_before_retry(retry_delay))
			return false;
		retry_delay = std::min(
			retry_delay * 2,
			m_options.maximum_retry_delay_ms);
	}
	return false;
}

bool HttpRangeByteSource::wait_for_block(std::uint64_t block_index)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	if (m_cache.find(block_index) != m_cache.end())
		return true;
	m_requested_block = block_index;
	m_condition.notify_all();
	m_condition.wait(lock, [&]()
	{
		return m_cache.find(block_index) != m_cache.end() ||
			!m_error.empty() || is_cancelled() || m_worker_stopped;
	});
	return m_cache.find(block_index) != m_cache.end();
}

void HttpRangeByteSource::insert_block(
	std::uint64_t block_index,
	std::vector<std::uint8_t> bytes)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto existing = m_cache.find(block_index);
	if (existing != m_cache.end())
		m_cache_bytes -= existing->second.bytes.size();
	m_cache_bytes += bytes.size();
	m_cache[block_index] = {std::move(bytes), ++m_use_counter};
	evict_to_budget(block_index);
	if (m_state == ByteSourceState::Rebuffering)
		++m_recovery_count;
	m_state = ByteSourceState::Ready;
	m_ready = true;
	m_condition.notify_all();
}

void HttpRangeByteSource::evict_to_budget(std::uint64_t protected_block)
{
	while (m_cache_bytes > m_options.maximum_cache_bytes)
	{
		auto victim = m_cache.end();
		for (auto iterator = m_cache.begin(); iterator != m_cache.end(); ++iterator)
		{
			if (iterator->first == protected_block)
				continue;
			if (victim == m_cache.end() ||
				iterator->second.last_use < victim->second.last_use)
				victim = iterator;
		}
		if (victim == m_cache.end())
			break;
		m_cache_bytes -= victim->second.bytes.size();
		m_cache.erase(victim);
	}
}

void HttpRangeByteSource::fail(std::string message)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!is_cancelled())
			m_error = std::move(message);
		m_state = is_cancelled()
			? ByteSourceState::Cancelled
			: ByteSourceState::Error;
		m_worker_stopped = true;
	}
	m_condition.notify_all();
}

void HttpRangeByteSource::set_state(ByteSourceState state)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_state = state;
	}
	m_condition.notify_all();
}

bool HttpRangeByteSource::wait_before_retry(long delay_ms)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_condition.wait_for(
		lock,
		std::chrono::milliseconds(delay_ms),
		[&]() { return is_cancelled(); });
	return !is_cancelled();
}

} // namespace openvolumetric
