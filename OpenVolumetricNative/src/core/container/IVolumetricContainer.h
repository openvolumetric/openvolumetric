#pragma once

#include <IByteSource.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openvolumetric
{

enum class StreamKind
{
	Video,
	Audio,
	Geometry,
	Unknown
};

struct Rational
{
	/// Numerator and denominator of a media timestamp time base.
	int numerator = 0;
	int denominator = 1;
};

/// One compressed sample read from a volumetric container.
///
/// Timestamps remain in the stream's time base. The payload owns its bytes,
/// so it can safely cross the container/decoder boundary.
struct ContainerPacket
{
	StreamKind kind = StreamKind::Unknown;
	int stream_index = -1;
	std::int64_t pts = 0;
	std::int64_t dts = 0;
	std::int64_t duration = 0;
	bool has_pts = false;
	bool has_dts = false;
	Rational time_base;
	std::vector<std::uint8_t> payload;
};

/// Engine-independent lifecycle and packet interface for volumetric files.
class IVolumetricContainer
{
public:
	/// Releases implementation-owned container resources.
	virtual ~IVolumetricContainer() = default;

	/// Opens and validates a combined volumetric media file.
	virtual bool open(const char* path) = 0;
	/// Opens and validates media through an owned custom byte source.
	virtual bool open(std::unique_ptr<IByteSource> source) = 0;
	/// Closes the file and clears stream discovery and error state.
	virtual void close() = 0;
	/// Reads the next recognized compressed sample in demux order.
	virtual bool read(ContainerPacket& packet) = 0;
	/// Repositions demuxing to a timestamp in seconds.
	virtual bool seek(double seconds) = 0;
	/// Interrupts blocking source I/O before the demux worker is joined.
	virtual void cancel_pending_io() = 0;

	/// Returns whether a container is currently available for reads.
	virtual bool is_open() const = 0;
	/// Returns whether read() reached the physical end of the container.
	virtual bool end_of_stream() const = 0;
	/// Returns the most recent container error.
	virtual const std::string& error() const = 0;
	/// Returns transport/cache activity for local or remote custom input.
	virtual ByteSourceDiagnostics source_diagnostics() const = 0;
};

} // namespace openvolumetric
