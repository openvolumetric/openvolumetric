#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openvolumetric
{

/// Decodes one sequential Draco position point cloud and validates its point
/// count before exposing positions in canonical vertex order.
bool decode_draco_point_cloud_positions(
	const std::uint8_t* data,
	std::size_t size,
	std::size_t expected_vertex_count,
	std::vector<float>& positions,
	std::string& error);

} // namespace openvolumetric
