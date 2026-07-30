#pragma once

#include "IByteSource.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace openvolumetric
{

/// Bounded cache and timeout settings for HTTP range input.
struct HttpRangeByteSourceOptions
{
	// Larger ranges amortize HTTP and Wi-Fi latency on standalone headsets.
	// The cache retains enough compressed input for short network stalls while
	// remaining small relative to the decoded media buffers.
	std::size_t block_size = 1024 * 1024;
	std::size_t maximum_cache_bytes = 32 * 1024 * 1024;
	std::size_t sequential_read_ahead_blocks = 3;
	long connection_timeout_ms = 10000;
	long request_timeout_ms = 10000;
	int maximum_retry_count = 6;
	long initial_retry_delay_ms = 250;
	long maximum_retry_delay_ms = 4000;
};

/// Seekable HTTP/HTTPS byte source backed by cancellable range requests.
///
/// A dedicated worker owns the libcurl handle. The FFmpeg demux thread waits
/// only for requested blocks to enter the bounded cache and never performs
/// socket I/O itself.
class HttpRangeByteSource final : public IByteSource
{
public:
	explicit HttpRangeByteSource(
		std::string url,
		HttpRangeByteSourceOptions options = {});
	~HttpRangeByteSource() override;

	HttpRangeByteSource(const HttpRangeByteSource&) = delete;
	HttpRangeByteSource& operator=(const HttpRangeByteSource&) = delete;

	std::int64_t read(
		std::uint8_t* destination,
		std::size_t capacity) override;
	std::int64_t seek(
		std::int64_t offset,
		ByteSeekOrigin origin) override;
	std::int64_t size() const override;
	bool is_seekable() const override;
	void cancel() override;
	bool is_cancelled() const override;
	const std::string& error() const override;
	ByteSourceDiagnostics diagnostics() const override;

	/// Returns whether metadata discovery and the initial range succeeded.
	bool is_open() const;

private:
	struct CacheBlock
	{
		std::vector<std::uint8_t> bytes;
		std::uint64_t last_use = 0;
	};

	void worker_loop();
	bool discover_resource(void* handle);
	bool download_block(void* handle, std::uint64_t block_index);
	bool wait_for_block(std::uint64_t block_index);
	void insert_block(
		std::uint64_t block_index,
		std::vector<std::uint8_t> bytes);
	void evict_to_budget(std::uint64_t protected_block);
	void fail(std::string message);
	void set_state(ByteSourceState state);
	bool wait_before_retry(long delay_ms);

	std::string m_url;
	HttpRangeByteSourceOptions m_options;
	mutable std::mutex m_mutex;
	std::condition_variable m_condition;
	std::map<std::uint64_t, CacheBlock> m_cache;
	std::optional<std::uint64_t> m_requested_block;
	std::thread m_worker;
	std::atomic<bool> m_cancelled{false};
	std::int64_t m_size = -1;
	std::int64_t m_position = 0;
	std::size_t m_cache_bytes = 0;
	std::uint64_t m_downloaded_bytes = 0;
	std::uint64_t m_request_count = 0;
	std::uint64_t m_recovery_count = 0;
	std::uint64_t m_use_counter = 0;
	ByteSourceState m_state = ByteSourceState::Opening;
	bool m_ready = false;
	bool m_worker_stopped = false;
	std::string m_error;
};

} // namespace openvolumetric
