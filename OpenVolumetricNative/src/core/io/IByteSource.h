#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openvolumetric
{

/// Origin used when repositioning a seekable byte source.
enum class ByteSeekOrigin
{
	Begin,
	Current,
	End
};

/// Observable lifecycle of a byte source.
enum class ByteSourceState
{
	Opening = 0,
	Ready = 1,
	Rebuffering = 2,
	Error = 3,
	Cancelled = 4,
	Ended = 5
};

/// Thread-safe transport/cache snapshot suitable for engine diagnostics.
struct ByteSourceDiagnostics
{
	ByteSourceState state = ByteSourceState::Opening;
	bool remote = false;
	std::int64_t resource_size_bytes = -1;
	std::uint64_t cached_bytes = 0;
	std::uint64_t downloaded_bytes = 0;
	/// EWMA of completed HTTP range-transfer rates; excludes cache idle time.
	std::uint64_t transfer_throughput_bits_per_second = 0;
	std::uint64_t request_count = 0;
	std::uint64_t recovery_count = 0;
	bool fragmented = false;
	std::uint64_t fragment_count = 0;
	std::int64_t active_fragment = -1;
	std::uint64_t cached_fragment_count = 0;
};

/// Recognizes the supported remote schemes without imposing URL case rules.
inline bool is_http_url(std::string_view value)
{
	const auto starts_with_case_insensitive =
		[value](std::string_view prefix)
		{
			if (value.size() < prefix.size())
				return false;
			for (std::size_t index = 0; index < prefix.size(); ++index)
			{
				char character = value[index];
				if (character >= 'A' && character <= 'Z')
					character = static_cast<char>(character - 'A' + 'a');
				if (character != prefix[index])
					return false;
			}
			return true;
		};
	return starts_with_case_insensitive("http://") ||
		starts_with_case_insensitive("https://");
}

/// Engine-independent byte input consumed by container custom I/O.
///
/// The container's demux thread is the sole caller of read() and seek().
/// cancel() may be called by another thread and must promptly unblock future
/// or in-progress I/O. Implementations own any file, network, and cache state.
class IByteSource
{
public:
	virtual ~IByteSource() = default;

	/// Reads up to capacity bytes, returning zero at end of resource and a
	/// negative value on cancellation or failure.
	virtual std::int64_t read(
		std::uint8_t* destination,
		std::size_t capacity) = 0;

	/// Repositions the next read and returns its absolute byte offset.
	virtual std::int64_t seek(
		std::int64_t offset,
		ByteSeekOrigin origin) = 0;

	/// Returns the resource size in bytes, or a negative value if unknown.
	virtual std::int64_t size() const = 0;
	/// Returns whether arbitrary byte seeking is supported.
	virtual bool is_seekable() const = 0;
	/// Requests cancellation without waiting for the demux thread.
	virtual void cancel() = 0;
	/// Returns whether cancellation has been requested.
	virtual bool is_cancelled() const = 0;
	/// Returns the most recent source-specific diagnostic.
	virtual const std::string& error() const = 0;
	/// Returns a non-blocking snapshot of transport and cache activity.
	virtual ByteSourceDiagnostics diagnostics() const = 0;
};

} // namespace openvolumetric
