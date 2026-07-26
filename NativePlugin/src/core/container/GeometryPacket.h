#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace volumetric_video
{

// Binary framing used inside every sample of the MP4 `vvge` data track.
// All integer fields are serialized little-endian. The 16-byte header is:
// magic "VVGF", version, flags, source frame number, and payload byte count.
constexpr std::size_t kGeometryPacketHeaderSize = 16;
constexpr std::uint16_t kGeometryPacketVersion = 1;
constexpr std::uint16_t kGeometryPacketKeyframe = 1;

/// A validated packet plus its still-compressed Draco payload.
struct GeometryPacket
{
	std::uint16_t version = kGeometryPacketVersion;
	std::uint16_t flags = kGeometryPacketKeyframe;
	std::uint32_t frame_number = 0;
	std::vector<std::uint8_t> payload;
};

/// Creates the complete VVGF sample written to the MP4 geometry track.
std::vector<std::uint8_t> serialize_geometry_packet(
	const GeometryPacket& packet);

/// Validates a VVGF sample and extracts its payload. Returns false for an
/// invalid magic value, unsupported version, or inconsistent payload size.
bool parse_geometry_packet(
	const std::uint8_t* data,
	std::size_t size,
	GeometryPacket& packet);

} // namespace volumetric_video
