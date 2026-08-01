#include "AdaptivePackage.h"

#include "AdaptiveManifest.h"
#include "FragmentedMp4Index.h"

extern "C"
{
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

namespace openvolumetric::authoring
{
namespace
{

namespace fs = std::filesystem;

struct ProbedRepresentation
{
	double duration_seconds = 0.0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint64_t resource_bitrate = 0;
	std::string video_codec;
	bool has_audio = false;
	std::size_t fragment_count = 0;
};

bool count_fragments(
	const fs::path& path,
	std::size_t& count,
	std::string& error)
{
	const std::uint64_t size = fs::file_size(path);
	const std::uint64_t tail_size = std::min<std::uint64_t>(size, 1024 * 1024);
	std::vector<std::uint8_t> tail(static_cast<std::size_t>(tail_size));
	std::ifstream input(path, std::ios::binary);
	input.seekg(static_cast<std::streamoff>(size - tail_size));
	if (!input.read(
			reinterpret_cast<char*>(tail.data()),
			static_cast<std::streamsize>(tail.size())))
	{
		error = "Could not read fragmented MP4 index from " + path.string() + ".";
		return false;
	}
	const auto fragments = parse_fragmented_mp4_index(
		tail.data(), tail.size(), size - tail_size, size);
	if (fragments.empty())
	{
		error = "Adaptive representation is not an indexed fragmented MP4: " +
			path.string();
		return false;
	}
	count = fragments.size();
	return true;
}

bool probe_representation(
	const fs::path& path,
	ProbedRepresentation& output,
	std::string& error)
{
	if (!fs::is_regular_file(path))
	{
		error = "Adaptive representation does not exist: " + path.string();
		return false;
	}
	AVFormatContext* context = nullptr;
	if (avformat_open_input(&context, path.string().c_str(), nullptr, nullptr) < 0 ||
		avformat_find_stream_info(context, nullptr) < 0)
	{
		if (context != nullptr)
			avformat_close_input(&context);
		error = "Could not inspect adaptive representation: " + path.string();
		return false;
	}

	for (unsigned int index = 0; index < context->nb_streams; ++index)
	{
		const AVCodecParameters* codec = context->streams[index]->codecpar;
		if (codec->codec_type == AVMEDIA_TYPE_VIDEO && output.width == 0)
		{
			output.width = static_cast<std::uint32_t>(codec->width);
			output.height = static_cast<std::uint32_t>(codec->height);
			output.video_codec = avcodec_get_name(codec->codec_id);
		}
		else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			output.has_audio = true;
		}
	}
	if (context->duration > 0)
	{
		output.duration_seconds =
			static_cast<double>(context->duration) / AV_TIME_BASE;
	}
	avformat_close_input(&context);
	if (output.width == 0 || output.height == 0 ||
		output.video_codec.empty() || output.duration_seconds <= 0.0)
	{
		error = "Adaptive representation has incomplete video metadata: " +
			path.string();
		return false;
	}
	output.resource_bitrate = static_cast<std::uint64_t>(std::ceil(
		static_cast<double>(fs::file_size(path)) * 8.0 /
		output.duration_seconds));
	return count_fragments(path, output.fragment_count, error);
}

std::string relative_uri(const fs::path& resource, const fs::path& manifest)
{
	std::error_code error;
	const fs::path relative = fs::relative(
		fs::absolute(resource), fs::absolute(manifest).parent_path(), error);
	return (error ? resource.filename() : relative).generic_string();
}

} // namespace

bool write_adaptive_package_manifest(
	const AdaptivePackageOptions& options,
	std::string& error)
{
	error.clear();
	if (options.presentation_id.empty() || options.manifest_path.empty() ||
		options.segment_duration_seconds <= 0.0 ||
		options.representations.size() < 2)
	{
		error = "Adaptive package requires an ID, manifest, segment duration, and two representations.";
		return false;
	}

	std::vector<ProbedRepresentation> probes(options.representations.size());
	for (std::size_t index = 0; index < options.representations.size(); ++index)
	{
		if (!probe_representation(
				options.representations[index].resource_path,
				probes[index],
				error))
		{
			return false;
		}
	}
	const double duration = probes.front().duration_seconds;
	const std::size_t fragment_count = probes.front().fragment_count;
	for (const ProbedRepresentation& probe : probes)
	{
		if (std::abs(probe.duration_seconds - duration) > 0.05 ||
			probe.fragment_count != fragment_count ||
			probe.has_audio != probes.front().has_audio)
		{
			error = "Adaptive representations do not share duration, fragments, and audio layout.";
			return false;
		}
	}

	nlohmann::json json = {
		{"format", "openvolumetric-adaptive"},
		{"version", AdaptiveManifest::supported_version},
		{"presentation_id", options.presentation_id},
		{"duration_seconds", duration},
		{"segment_duration_seconds", options.segment_duration_seconds},
		{"has_audio", probes.front().has_audio},
		{"segments", nlohmann::json::array()},
		{"representations", nlohmann::json::array()}};
	for (std::size_t index = 0; index < fragment_count; ++index)
	{
		const double start = index * options.segment_duration_seconds;
		const double segment_duration = index + 1 < fragment_count
			? options.segment_duration_seconds
			: duration - start;
		if (segment_duration <= 0.0)
		{
			error = "Fragment count exceeds the declared adaptive timeline.";
			return false;
		}
		json["segments"].push_back({
			{"number", index},
			{"start_seconds", start},
			{"duration_seconds", segment_duration}});
	}
	for (std::size_t index = 0; index < options.representations.size(); ++index)
	{
		const AdaptivePackageRepresentation& input = options.representations[index];
		const ProbedRepresentation& probe = probes[index];
		const std::uint64_t geometry_bitrate = std::max<std::uint64_t>(
			1, static_cast<std::uint64_t>(std::ceil(
				static_cast<double>(input.geometry_payload_bytes) * 8.0 / duration)));
		const std::uint64_t texture_bitrate = std::max<std::uint64_t>(
			1, probe.resource_bitrate > geometry_bitrate
				? probe.resource_bitrate - geometry_bitrate
				: 1);
		json["representations"].push_back({
			{"id", input.id},
			{"resource_uri", relative_uri(input.resource_path, options.manifest_path)},
			{"compatibility_group", input.compatibility_group},
			{"bandwidth", probe.resource_bitrate},
			{"texture", {
				{"codec", probe.video_codec},
				{"width", probe.width},
				{"height", probe.height},
				{"bitrate", texture_bitrate}}},
			{"geometry", {
				{"codec", "openvolumetric-vvge-draco-v2"},
				{"position_quantization_bits", input.position_quantization_bits},
				{"bitrate", geometry_bitrate},
				{"temporal_compression", input.temporal_compression}}}});
	}

	AdaptiveManifest parsed;
	const std::string contents = json.dump(2) + "\n";
	if (!AdaptiveManifestParser::parse(contents, parsed, error))
		return false;

	if (!options.manifest_path.parent_path().empty())
		fs::create_directories(options.manifest_path.parent_path());
	fs::path temporary = options.manifest_path;
	temporary += ".tmp";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
	output.close();
	if (!output)
	{
		error = "Could not write adaptive manifest: " + temporary.string();
		return false;
	}
	std::error_code ignored;
	fs::remove(options.manifest_path, ignored);
	fs::rename(temporary, options.manifest_path, ignored);
	if (ignored)
	{
		error = "Could not publish adaptive manifest: " + ignored.message();
		fs::remove(temporary, ignored);
		return false;
	}
	return true;
}

} // namespace openvolumetric::authoring
