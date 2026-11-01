#pragma once

#include <OpenVolumetricPlayer.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace openvolumetric
{

/// Terminal result of one demand-driven presentation request.
enum class FrameRequestResult
{
	Ready,
	InvalidTime,
	NotOpen,
	Missing,
	Timeout,
	Cancelled,
	Failed
};

/// Resource limits for deterministic host-driven frame evaluation.
struct OpenVolumetricFrameReaderOptions
{
	/// Maximum number of complete presentations retained by the LRU cache.
	std::size_t maximum_cached_presentations = 8;
	/// Maximum estimated payload bytes retained by the LRU cache.
	std::size_t maximum_cache_bytes = 256U * 1024U * 1024U;
	/// Maximum time spent waiting for one seek and decode operation.
	std::uint32_t request_timeout_milliseconds = 5000;
};

/// Thread-safe snapshot of demand-driven cache activity.
struct OpenVolumetricFrameReaderDiagnostics
{
	std::uint64_t cache_hits = 0;
	std::uint64_t cache_misses = 0;
	std::uint64_t coalesced_requests = 0;
	std::uint64_t cache_evictions = 0;
	std::size_t cached_presentations = 0;
	std::size_t cached_bytes = 0;
};

/// Demand-driven random-access façade for timeline and offline hosts.
///
/// Unlike OpenVolumetricPlayer, this class owns no advancing presentation
/// clock. Each request seeks and decodes an exact media time, then returns a
/// shared immutable presentation. Requests for the same time are coalesced;
/// different cache misses are serialized because the underlying FFmpeg and
/// Draco session has one mutable seek/decode state.
class OpenVolumetricFrameReader final
{
public:
	explicit OpenVolumetricFrameReader(
		OpenVolumetricFrameReaderOptions options = {});
	~OpenVolumetricFrameReader();

	OpenVolumetricFrameReader(const OpenVolumetricFrameReader&) = delete;
	OpenVolumetricFrameReader& operator=(const OpenVolumetricFrameReader&) =
		delete;

	/// Opens and starts one local or HTTP(S) OpenVolumetric representation.
	bool open(const char* source);
	/// Cancels pending input and idempotently releases the decoder and cache.
	void close();
	/// Cancels current requests without closing the source or clearing the cache.
	///
	/// Timeline hosts call this when a newer evaluation makes outstanding work
	/// obsolete. A request already blocked inside transport I/O may return only
	/// after that bounded operation completes, but its result is discarded.
	void cancel_pending_requests();
	/// Requests one immutable texture-and-geometry presentation at media time.
	FrameRequestResult request(
		double presentation_time,
		std::shared_ptr<const OpenVolumetricPresentation>& output);

	/// Returns a value snapshot that remains valid across later lifecycle calls.
	OpenVolumetricMediaInfo media_info() const;
	/// Returns current cache counters and bounded-storage use.
	OpenVolumetricFrameReaderDiagnostics diagnostics() const;
	/// Returns the most recent lifecycle or request failure message.
	std::string error() const;

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace openvolumetric
