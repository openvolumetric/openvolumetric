#include "GeometryPacket.h"

#include <limits>

namespace openvolumetric
{
namespace
{

/// Writes a 16-bit integer in the format's canonical little-endian order.
void write_u16(std::uint8_t* output, std::uint16_t value)
{
	output[0] = static_cast<std::uint8_t>(value >> 8);
	output[1] = static_cast<std::uint8_t>(value);
}

/// Writes a 32-bit integer in the format's canonical little-endian order.
void write_u32(std::uint8_t* output, std::uint32_t value)
{
	output[0] = static_cast<std::uint8_t>(value >> 24);
	output[1] = static_cast<std::uint8_t>(value >> 16);
	output[2] = static_cast<std::uint8_t>(value >> 8);
	output[3] = static_cast<std::uint8_t>(value);
}

/// Reads a little-endian 16-bit field without alignment assumptions.
std::uint16_t read_u16(const std::uint8_t* input)
{
	return static_cast<std::uint16_t>(
		(static_cast<std::uint16_t>(input[0]) << 8) |
		static_cast<std::uint16_t>(input[1]));
}

/// Reads a little-endian 32-bit field without alignment assumptions.
std::uint32_t read_u32(const std::uint8_t* input)
{
	return
		(static_cast<std::uint32_t>(input[0]) << 24) |
		(static_cast<std::uint32_t>(input[1]) << 16) |
		(static_cast<std::uint32_t>(input[2]) << 8) |
		static_cast<std::uint32_t>(input[3]);
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

	std::vector<std::uint8_t> output(
		kGeometryPacketHeaderSize + packet.payload.size());
	output[0] = 'V';
	output[1] = 'V';
	output[2] = 'G';
	output[3] = 'F';
	write_u16(output.data() + 4, packet.version);
	write_u16(output.data() + 6, packet.flags);
	write_u32(output.data() + 8, packet.frame_number);
	write_u32(
		output.data() + 12,
		static_cast<std::uint32_t>(packet.payload.size()));

	for (std::size_t i = 0; i < packet.payload.size(); ++i)
	{
		output[kGeometryPacketHeaderSize + i] = packet.payload[i];
	}
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

	const std::uint32_t payload_size = read_u32(data + 12);
	if (payload_size != size - kGeometryPacketHeaderSize)
	{
		return false;
	}

	packet.version = read_u16(data + 4);
	packet.flags = read_u16(data + 6);
	packet.frame_number = read_u32(data + 8);
	packet.payload.assign(
		data + kGeometryPacketHeaderSize,
		data + size);
	return packet.version == kGeometryPacketVersion;
}

} // namespace openvolumetric
