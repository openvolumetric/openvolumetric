#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openvolumetric
{

// Binary framing used inside every sample of the MP4 `vvge` data track.
// All integer fields are serialized in big-endian/network byte order.
constexpr std::size_t kGeometryPacketHeaderSize = 40;
constexpr std::uint16_t kGeometryPacketVersion = 2;
constexpr std::uint16_t kGeometryPacketKeyframe = 1;

/// Geometry representation carried by a geometry packet.
enum class GeometryCodingMode : std::uint8_t
{
	IndependentMesh = 0,
	PositionUpdate = 1
};

/// Payload encoding used by a geometry packet.
enum class GeometryPayloadCodec : std::uint8_t
{
	DracoMesh = 0,
	DracoPointCloud = 1
};

/// A validated packet plus its still-compressed Draco payload.
struct GeometryPacket
{
	std::uint16_t version = kGeometryPacketVersion;
	std::uint16_t flags = kGeometryPacketKeyframe;
	std::uint32_t frame_number = 0;
	GeometryCodingMode coding_mode = GeometryCodingMode::IndependentMesh;
	GeometryPayloadCodec payload_codec = GeometryPayloadCodec::DracoMesh;
	std::uint64_t topology_id = 0;
	std::uint32_t keyframe_frame_number = 0;
	std::uint32_t vertex_count = 0;
	std::uint32_t triangle_count = 0;
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

} // namespace openvolumetric
