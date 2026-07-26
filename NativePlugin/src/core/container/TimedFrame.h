#pragma once

#include <cstdint>
#include <vector>

namespace volumetric_video
{

/// Compressed geometry sample with container-derived presentation timing.
struct CompressedGeometryFrame
{
	int frame_index = -1;
	double presentation_time = 0.0;
	std::uint32_t source_frame_number = 0;
	std::vector<std::uint8_t> payload;
};

} // namespace volumetric_video
