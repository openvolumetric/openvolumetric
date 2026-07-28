#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openvolumetric::authoring
{

/// Encodes canonical render-vertex positions as an order-preserving sequential
/// Draco point cloud for a PositionUpdate packet.
bool encode_positions_to_draco_point_cloud(
	const std::vector<float>& positions,
	int quantization_bits,
	int encode_speed,
	int decode_speed,
	std::vector<std::uint8_t>& output,
	std::string& error);

} // namespace openvolumetric::authoring
