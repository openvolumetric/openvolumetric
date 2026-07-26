#pragma once

#include <cstdint>
#include <vector>

namespace volumetric_video
{

enum class FrameMatchResult
{
	Ready,
	NotReady,
	Missing
};

/// Compressed geometry sample with container-derived presentation timing.
struct CompressedGeometryFrame
{
	std::uint64_t generation = 0;
	double presentation_time = 0.0;
	std::uint32_t source_frame_number = 0;
	std::vector<std::uint8_t> payload;
};

} // namespace volumetric_video
