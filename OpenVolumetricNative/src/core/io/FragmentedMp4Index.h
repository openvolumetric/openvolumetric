#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openvolumetric
{

/// Byte range of one independently addressable fragmented-MP4 media segment.
struct Mp4FragmentRange
{
	std::uint64_t offset = 0;
	std::uint64_t size = 0;
};

/// Parses the terminal `mfra` random-access index written by the authoring
/// pipeline. The input must end at the end of the complete MP4 resource.
/// Invalid, absent, or incomplete indexes return an empty result so ordinary
/// progressive MP4 input continues through the byte-block fallback path.
std::vector<Mp4FragmentRange> parse_fragmented_mp4_index(
	const std::uint8_t* tail,
	std::size_t tail_size,
	std::uint64_t tail_offset,
	std::uint64_t resource_size);

} // namespace openvolumetric
