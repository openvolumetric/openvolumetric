#include "FragmentedMp4Index.h"

#include <algorithm>
#include <limits>

namespace openvolumetric
{
namespace
{

std::uint32_t read_u32(const std::uint8_t* value)
{
	return (static_cast<std::uint32_t>(value[0]) << 24) |
		(static_cast<std::uint32_t>(value[1]) << 16) |
		(static_cast<std::uint32_t>(value[2]) << 8) |
		static_cast<std::uint32_t>(value[3]);
}

std::uint64_t read_u64(const std::uint8_t* value)
{
	return (static_cast<std::uint64_t>(read_u32(value)) << 32) |
		read_u32(value + 4);
}

bool is_type(const std::uint8_t* value, const char (&type)[5])
{
	return value[0] == static_cast<std::uint8_t>(type[0]) &&
		value[1] == static_cast<std::uint8_t>(type[1]) &&
		value[2] == static_cast<std::uint8_t>(type[2]) &&
		value[3] == static_cast<std::uint8_t>(type[3]);
}

} // namespace

std::vector<Mp4FragmentRange> parse_fragmented_mp4_index(
	const std::uint8_t* tail,
	std::size_t tail_size,
	std::uint64_t tail_offset,
	std::uint64_t resource_size)
{
	std::vector<Mp4FragmentRange> result;
	if (tail == nullptr || tail_size < 16 || resource_size < 16)
		return result;

	const std::uint8_t* mfro = tail + tail_size - 16;
	if (read_u32(mfro) != 16 || !is_type(mfro + 4, "mfro"))
		return result;
	const std::uint32_t mfra_size = read_u32(mfro + 12);
	if (mfra_size < 24 || mfra_size > tail_size || mfra_size > resource_size)
		return result;

	const std::size_t mfra_position = tail_size - mfra_size;
	const std::uint8_t* mfra = tail + mfra_position;
	if (read_u32(mfra) != mfra_size || !is_type(mfra + 4, "mfra") ||
		tail_offset + mfra_position + mfra_size != resource_size)
		return result;

	// Every authored track has equivalent fragment offsets. Parse the first
	// valid tfra only, avoiding three copies of the same scheduling boundary.
	std::size_t position = mfra_position + 8;
	const std::size_t end = tail_size - 16;
	while (position + 8 <= end)
	{
		const std::uint32_t box_size = read_u32(tail + position);
		if (box_size < 8 || box_size > end - position)
			return {};
		if (!is_type(tail + position + 4, "tfra"))
		{
			position += box_size;
			continue;
		}
		if (box_size < 24)
			return {};

		const std::uint8_t version = tail[position + 8];
		const std::uint32_t lengths = read_u32(tail + position + 16);
		const std::size_t traf_length = ((lengths >> 4) & 3) + 1;
		const std::size_t trun_length = ((lengths >> 2) & 3) + 1;
		const std::size_t sample_length = (lengths & 3) + 1;
		const std::uint32_t entry_count = read_u32(tail + position + 20);
		std::size_t entry = position + 24;
		std::vector<std::uint64_t> offsets;
		offsets.reserve(entry_count);
		for (std::uint32_t index = 0; index < entry_count; ++index)
		{
			const std::size_t fixed_size = version == 1 ? 16 : 8;
			const std::size_t entry_size = fixed_size + traf_length +
				trun_length + sample_length;
			if (entry_size > position + box_size - entry)
				return {};
			const std::uint64_t offset = version == 1
				? read_u64(tail + entry + 8)
				: read_u32(tail + entry + 4);
			if (offset >= resource_size ||
				(!offsets.empty() && offset <= offsets.back()))
				return {};
			offsets.push_back(offset);
			entry += entry_size;
		}
		if (offsets.empty())
			return {};
		for (std::size_t index = 0; index < offsets.size(); ++index)
		{
			const std::uint64_t fragment_end = index + 1 < offsets.size()
				? offsets[index + 1]
				: tail_offset + mfra_position;
			if (fragment_end <= offsets[index])
				return {};
			result.push_back({offsets[index], fragment_end - offsets[index]});
		}
		return result;
	}
	return result;
}

} // namespace openvolumetric
