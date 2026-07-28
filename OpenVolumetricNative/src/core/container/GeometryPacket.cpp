#include "GeometryPacket.h"

#include <algorithm>
#include <limits>

namespace openvolumetric
{
namespace
{

/// Writes a 16-bit integer in the format's canonical big-endian order.
void write_u16(std::uint8_t* output, std::uint16_t value)
{
	output[0] = static_cast<std::uint8_t>(value >> 8);
	output[1] = static_cast<std::uint8_t>(value);
}

/// Writes a 32-bit integer in the format's canonical big-endian order.
void write_u32(std::uint8_t* output, std::uint32_t value)
{
	output[0] = static_cast<std::uint8_t>(value >> 24);
	output[1] = static_cast<std::uint8_t>(value >> 16);
	output[2] = static_cast<std::uint8_t>(value >> 8);
	output[3] = static_cast<std::uint8_t>(value);
}

/// Writes a 64-bit integer in the format's canonical big-endian order.
void write_u64(std::uint8_t* output, std::uint64_t value)
{
	write_u32(output, static_cast<std::uint32_t>(value >> 32));
	write_u32(output + 4, static_cast<std::uint32_t>(value));
}

/// Reads a big-endian 16-bit field without alignment assumptions.
std::uint16_t read_u16(const std::uint8_t* input)
{
	return static_cast<std::uint16_t>(
		(static_cast<std::uint16_t>(input[0]) << 8) |
		static_cast<std::uint16_t>(input[1]));
}

/// Reads a big-endian 32-bit field without alignment assumptions.
std::uint32_t read_u32(const std::uint8_t* input)
{
	return
		(static_cast<std::uint32_t>(input[0]) << 24) |
		(static_cast<std::uint32_t>(input[1]) << 16) |
		(static_cast<std::uint32_t>(input[2]) << 8) |
		static_cast<std::uint32_t>(input[3]);
}

/// Reads a big-endian 64-bit field without alignment assumptions.
std::uint64_t read_u64(const std::uint8_t* input)
{
	return
		(static_cast<std::uint64_t>(read_u32(input)) << 32) |
		static_cast<std::uint64_t>(read_u32(input + 4));
}

bool valid_combination(
	GeometryCodingMode mode,
	GeometryPayloadCodec codec,
	std::uint16_t flags)
{
	if (mode == GeometryCodingMode::IndependentMesh)
	{
		return codec == GeometryPayloadCodec::DracoMesh &&
			flags == kGeometryPacketKeyframe;
	}
	if (mode == GeometryCodingMode::PositionUpdate)
	{
		return codec == GeometryPayloadCodec::DracoPointCloud &&
			flags == 0;
	}
	return false;
}

} // namespace

std::vector<std::uint8_t> serialize_geometry_packet(
	const GeometryPacket& packet)
{
	if (packet.payload.size() >
		static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
	{
		return {};
	}

	if (packet.version != kGeometryPacketVersion ||
		!valid_combination(
			packet.coding_mode, packet.payload_codec, packet.flags) ||
		packet.topology_id == 0 ||
		packet.vertex_count == 0 || packet.triangle_count == 0 ||
		(packet.coding_mode == GeometryCodingMode::IndependentMesh &&
			packet.keyframe_frame_number != packet.frame_number) ||
		(packet.coding_mode == GeometryCodingMode::PositionUpdate &&
			packet.keyframe_frame_number >= packet.frame_number))
	{
		return {};
	}

	std::vector<std::uint8_t> output(
		kGeometryPacketHeaderSize + packet.payload.size(), 0);
	output[0] = 'V';
	output[1] = 'V';
	output[2] = 'G';
	output[3] = 'F';
	write_u16(output.data() + 4, kGeometryPacketVersion);
	write_u16(
		output.data() + 6,
		static_cast<std::uint16_t>(kGeometryPacketHeaderSize));
	output[8] = static_cast<std::uint8_t>(packet.coding_mode);
	output[9] = static_cast<std::uint8_t>(packet.payload_codec);
	write_u16(output.data() + 10, packet.flags);
	write_u32(output.data() + 12, packet.frame_number);
	write_u64(output.data() + 16, packet.topology_id);
	write_u32(output.data() + 24, packet.keyframe_frame_number);
	write_u32(output.data() + 28, packet.vertex_count);
	write_u32(output.data() + 32, packet.triangle_count);
	write_u32(
		output.data() + 36,
		static_cast<std::uint32_t>(packet.payload.size()));
	std::copy(
		packet.payload.begin(),
		packet.payload.end(),
		output.begin() + kGeometryPacketHeaderSize);
	return output;
}

bool parse_geometry_packet(
	const std::uint8_t* data,
	std::size_t size,
	GeometryPacket& packet)
{
	if (data == nullptr || size < kGeometryPacketHeaderSize ||
		data[0] != 'V' || data[1] != 'V' ||
		data[2] != 'G' || data[3] != 'F')
	{
		return false;
	}

	const std::uint16_t version = read_u16(data + 4);
	if (version != kGeometryPacketVersion)
	{
		return false;
	}
	const std::uint16_t header_size = read_u16(data + 6);
	if (header_size < kGeometryPacketHeaderSize || header_size > size)
		return false;
	const std::uint32_t payload_size = read_u32(data + 36);
	if (payload_size != size - header_size)
		return false;

	const auto mode = static_cast<GeometryCodingMode>(data[8]);
	const auto codec = static_cast<GeometryPayloadCodec>(data[9]);
	const std::uint16_t flags = read_u16(data + 10);
	const std::uint32_t frame_number = read_u32(data + 12);
	const std::uint32_t keyframe_frame_number = read_u32(data + 24);
	const std::uint32_t vertex_count = read_u32(data + 28);
	const std::uint32_t triangle_count = read_u32(data + 32);
	if (!valid_combination(mode, codec, flags) ||
		read_u64(data + 16) == 0 ||
		vertex_count == 0 || triangle_count == 0 ||
		(mode == GeometryCodingMode::IndependentMesh &&
			keyframe_frame_number != frame_number) ||
		(mode == GeometryCodingMode::PositionUpdate &&
			keyframe_frame_number >= frame_number))
	{
		return false;
	}

	packet = {};
	packet.version = version;
	packet.flags = flags;
	packet.frame_number = frame_number;
	packet.coding_mode = mode;
	packet.payload_codec = codec;
	packet.topology_id = read_u64(data + 16);
	packet.keyframe_frame_number = keyframe_frame_number;
	packet.vertex_count = vertex_count;
	packet.triangle_count = triangle_count;
	packet.payload.assign(
		data + header_size,
		data + size);
	return true;
}

} // namespace openvolumetric
