#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace volumetric_video
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
	virtual ~IVolumetricContainer() = default;

	virtual bool open(const char* path) = 0;
	virtual void close() = 0;
	virtual bool read(ContainerPacket& packet) = 0;
	virtual bool seek(double seconds) = 0;

	virtual bool is_open() const = 0;
	virtual bool end_of_stream() const = 0;
	virtual const std::string& error() const = 0;
};

} // namespace volumetric_video
