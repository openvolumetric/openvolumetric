#include "AdaptivePackage.h"

#include "AdaptiveManifest.h"
#include "FragmentedMp4Index.h"
#include "GeometryPacket.h"

extern "C"
{
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

#include <nlohmann/json.hpp>

namespace openvolumetric::authoring
{
namespace
{

namespace fs = std::filesystem;

struct ProbedRepresentation
{
	struct TimedSample
	{
		double time = 0.0;
		double duration = 0.0;
	};
	struct GeometrySample
	{
		double time = 0.0;
		std::uint32_t frame_number = 0;
		std::uint32_t keyframe_frame_number = 0;
		std::uint64_t topology_id = 0;
		std::uint32_t vertex_count = 0;
		std::uint32_t triangle_count = 0;
		GeometryCodingMode coding_mode = GeometryCodingMode::IndependentMesh;
	};

	double duration_seconds = 0.0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	std::uint64_t resource_bitrate = 0;
	std::string video_codec;
	double frame_rate = 0.0;
	bool has_audio = false;
	std::uint32_t audio_sample_rate = 0;
	std::uint32_t audio_channels = 0;
	double audio_start_time = std::numeric_limits<double>::infinity();
	double audio_end_time = -std::numeric_limits<double>::infinity();
	std::size_t fragment_count = 0;
	std::size_t geometry_sample_count = 0;
	std::vector<double> video_sample_times;
	std::vector<TimedSample> audio_samples;
	std::vector<GeometrySample> geometry_samples;
	std::vector<double> video_access_points;
	std::vector<double> geometry_access_points;
};

constexpr std::uint32_t make_tag(char a, char b, char c, char d)
{
	return static_cast<std::uint32_t>(a) |
		(static_cast<std::uint32_t>(b) << 8) |
		(static_cast<std::uint32_t>(c) << 16) |
		(static_cast<std::uint32_t>(d) << 24);
}

constexpr std::uint32_t geometry_tag = make_tag('v', 'v', 'g', 'e');

double packet_time(const AVPacket& packet, const AVStream& stream)
{
	const std::int64_t timestamp = packet.pts != AV_NOPTS_VALUE
		? packet.pts
		: packet.dts;
	return timestamp == AV_NOPTS_VALUE
		? std::numeric_limits<double>::quiet_NaN()
		: static_cast<double>(timestamp) * av_q2d(stream.time_base);
}

bool contains_access_point(
	const std::vector<double>& access_points,
	double boundary,
	double tolerance)
{
	return std::any_of(
		access_points.begin(), access_points.end(),
		[boundary, tolerance](double value)
		{
			return std::abs(value - boundary) <= tolerance;
		});
}

bool matching_timeline(
	const std::vector<double>& left,
	const std::vector<double>& right,
	double tolerance)
{
	if (left.size() != right.size())
		return false;
	for (std::size_t index = 0; index < left.size(); ++index)
	{
		if (std::abs(left[index] - right[index]) > tolerance)
			return false;
	}
	return true;
}

bool matching_audio_timeline(
	const std::vector<ProbedRepresentation::TimedSample>& left,
	const std::vector<ProbedRepresentation::TimedSample>& right,
	double tolerance)
{
	if (left.size() != right.size())
		return false;
	for (std::size_t index = 0; index < left.size(); ++index)
	{
		if (std::abs(left[index].time - right[index].time) > tolerance ||
			std::abs(left[index].duration - right[index].duration) > tolerance)
		{
			return false;
		}
	}
	return true;
}

bool matching_geometry_timeline(
	const std::vector<ProbedRepresentation::GeometrySample>& left,
	const std::vector<ProbedRepresentation::GeometrySample>& right)
{
	if (left.size() != right.size())
		return false;
	for (std::size_t index = 0; index < left.size(); ++index)
	{
		const auto& a = left[index];
		const auto& b = right[index];
		if (std::abs(a.time - b.time) > 0.000001 ||
			a.frame_number != b.frame_number ||
			a.keyframe_frame_number != b.keyframe_frame_number ||
			a.topology_id != b.topology_id ||
			a.vertex_count != b.vertex_count ||
			a.triangle_count != b.triangle_count ||
			a.coding_mode != b.coding_mode)
		{
			return false;
		}
	}
	return true;
}

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

	int video_stream_index = -1;
	int geometry_stream_index = -1;
	for (unsigned int index = 0; index < context->nb_streams; ++index)
	{
		const AVCodecParameters* codec = context->streams[index]->codecpar;
		if (codec->codec_type == AVMEDIA_TYPE_VIDEO && output.width == 0)
		{
			video_stream_index = static_cast<int>(index);
			output.width = static_cast<std::uint32_t>(codec->width);
			output.height = static_cast<std::uint32_t>(codec->height);
			output.video_codec = avcodec_get_name(codec->codec_id);
			output.frame_rate = av_q2d(context->streams[index]->avg_frame_rate);
		}
		else if (codec->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			output.has_audio = true;
			output.audio_sample_rate =
				static_cast<std::uint32_t>(codec->sample_rate);
			output.audio_channels =
				static_cast<std::uint32_t>(codec->ch_layout.nb_channels);
		}
		else if (codec->codec_type == AVMEDIA_TYPE_DATA &&
			codec->codec_tag == geometry_tag)
		{
			geometry_stream_index = static_cast<int>(index);
		}
	}
	if (context->duration > 0)
	{
		output.duration_seconds =
			static_cast<double>(context->duration) / AV_TIME_BASE;
	}
	if (output.width == 0 || output.height == 0 ||
		output.video_codec.empty() || output.duration_seconds <= 0.0 ||
		output.frame_rate <= 0.0 || video_stream_index < 0 ||
		geometry_stream_index < 0 ||
		(output.has_audio &&
			(output.audio_sample_rate == 0 || output.audio_channels == 0)))
	{
		avformat_close_input(&context);
		error = "Adaptive representation has incomplete video metadata: " +
			path.string();
		return false;
	}

	std::map<std::uint32_t, GeometryPacket> geometry_keyframes;
	AVPacket packet{};
	while (av_read_frame(context, &packet) >= 0)
	{
		const double time = packet_time(
			packet, *context->streams[packet.stream_index]);
		if (packet.stream_index == video_stream_index && std::isfinite(time))
		{
			output.video_sample_times.push_back(time);
			if ((packet.flags & AV_PKT_FLAG_KEY) != 0)
				output.video_access_points.push_back(time);
		}
		else if (packet.stream_index == geometry_stream_index)
		{
			GeometryPacket geometry;
			if (!std::isfinite(time) || !parse_geometry_packet(
					packet.data, static_cast<std::size_t>(packet.size), geometry))
			{
				av_packet_unref(&packet);
				avformat_close_input(&context);
				error = "Malformed geometry packet in adaptive representation: " +
					path.string();
				return false;
			}
			++output.geometry_sample_count;
			output.geometry_samples.push_back({
				time,
				geometry.frame_number,
				geometry.keyframe_frame_number,
				geometry.topology_id,
				geometry.vertex_count,
				geometry.triangle_count,
				geometry.coding_mode});
			if (geometry.coding_mode == GeometryCodingMode::IndependentMesh)
			{
				geometry_keyframes[geometry.frame_number] = geometry;
				if (std::isfinite(time))
					output.geometry_access_points.push_back(time);
			}
			else
			{
				const auto keyframe = geometry_keyframes.find(
					geometry.keyframe_frame_number);
				if (keyframe == geometry_keyframes.end() ||
					keyframe->second.topology_id != geometry.topology_id ||
					keyframe->second.vertex_count != geometry.vertex_count ||
					keyframe->second.triangle_count != geometry.triangle_count)
				{
					av_packet_unref(&packet);
					avformat_close_input(&context);
					error = "Geometry update has an unavailable or incompatible keyframe in: " +
						path.string();
					return false;
				}
			}
		}
		else if (context->streams[packet.stream_index]->codecpar->codec_type ==
			AVMEDIA_TYPE_AUDIO && std::isfinite(time))
		{
			output.audio_start_time = std::min(output.audio_start_time, time);
			const double duration = packet.duration > 0
				? static_cast<double>(packet.duration) * av_q2d(
					context->streams[packet.stream_index]->time_base)
				: 0.0;
			output.audio_end_time = std::max(
				output.audio_end_time, time + duration);
			output.audio_samples.push_back({time, duration});
		}
		av_packet_unref(&packet);
	}
	av_packet_unref(&packet);
	avformat_close_input(&context);
	if (output.geometry_sample_count == 0)
	{
		error = "Adaptive representation contains no geometry samples: " +
			path.string();
		return false;
	}
	if (output.has_audio &&
		(!std::isfinite(output.audio_start_time) ||
		 !std::isfinite(output.audio_end_time)))
	{
		error = "Adaptive representation declares audio but contains no timed audio packets: " +
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

std::string AdaptivePackageVerification::summary() const
{
	std::ostringstream report;
	report << "Verified " << representation_count << " representations, "
		<< segment_count << " aligned segments, "
		<< checked_switch_boundary_count << " switch boundaries, and "
		<< geometry_sample_count << " geometry samples per representation; "
		<< std::fixed << std::setprecision(3) << duration_seconds
		<< " seconds at " << frame_rate << " fps";
	if (has_audio)
		report << ", audio " << audio_sample_rate << " Hz / "
			<< audio_channels << " channels";
	report << ".";
	return report.str();
}

bool write_adaptive_package_manifest(
	const AdaptivePackageOptions& options,
	std::string& error,
	AdaptivePackageVerification* verification)
{
	error.clear();
	if (options.presentation_id.empty() || options.manifest_path.empty() ||
		options.segment_duration_seconds <= 0.0 ||
		options.representations.size() < 2)
	{
		error = "Adaptive package requires an ID, manifest, segment duration, and two representations.";
		return false;
	}
	const std::string& compatibility_group =
		options.representations.front().compatibility_group;
	std::vector<std::string> representation_ids;
	for (const AdaptivePackageRepresentation& representation :
		options.representations)
	{
		if (representation.id.empty() || representation.resource_path.empty() ||
			representation.compatibility_group.empty() ||
			representation.compatibility_group != compatibility_group ||
			std::find(
				representation_ids.begin(), representation_ids.end(),
				representation.id) != representation_ids.end())
		{
			error = "Adaptive representations require unique IDs and one non-empty compatibility group.";
			return false;
		}
		representation_ids.push_back(representation.id);
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
	const double frame_rate = probes.front().frame_rate;
	const double access_point_tolerance = std::max(0.001, 0.5 / frame_rate);
	for (std::size_t index = 0; index < probes.size(); ++index)
	{
		const ProbedRepresentation& probe = probes[index];
		if (std::abs(probe.duration_seconds - duration) > 0.05 ||
			probe.fragment_count != fragment_count ||
			probe.has_audio != probes.front().has_audio ||
			probe.video_codec != probes.front().video_codec ||
			std::abs(probe.frame_rate - frame_rate) > 0.001 ||
			probe.geometry_sample_count != probes.front().geometry_sample_count ||
			!matching_timeline(
				probe.video_sample_times,
				probes.front().video_sample_times,
				0.000001) ||
			!matching_geometry_timeline(
				probe.geometry_samples,
				probes.front().geometry_samples) ||
			(probe.has_audio &&
				(probe.audio_sample_rate != probes.front().audio_sample_rate ||
				 probe.audio_channels != probes.front().audio_channels ||
				 std::abs(
					 probe.audio_start_time - probes.front().audio_start_time) >
						1.0 / probe.audio_sample_rate ||
				 std::abs(
					 probe.audio_end_time - probes.front().audio_end_time) >
						1.0 / probe.audio_sample_rate ||
				 !matching_audio_timeline(
					 probe.audio_samples,
					 probes.front().audio_samples,
					 1.0 / probe.audio_sample_rate))))
		{
			error = "Adaptive representations do not share codec, duration, frame/geometry timelines, fragment count, and sample-aligned audio timing.";
			return false;
		}
		for (std::size_t segment = 0; segment < fragment_count; ++segment)
		{
			const double boundary =
				static_cast<double>(segment) * options.segment_duration_seconds;
			if (!contains_access_point(
					probe.video_access_points, boundary, access_point_tolerance))
			{
				error = "Adaptive representation '" +
					options.representations[index].id +
					"' has no video random-access point at segment boundary " +
					std::to_string(segment) + ".";
				return false;
			}
			if (!contains_access_point(
					probe.geometry_access_points, boundary, access_point_tolerance))
			{
				error = "Adaptive representation '" +
					options.representations[index].id +
					"' has no independent geometry sample at segment boundary " +
					std::to_string(segment) + ".";
				return false;
			}
		}
	}

	AdaptivePackageVerification verified;
	verified.representation_count = probes.size();
	verified.segment_count = fragment_count;
	verified.geometry_sample_count = probes.front().geometry_sample_count;
	verified.checked_switch_boundary_count =
		(fragment_count > 0 ? fragment_count - 1 : 0) * probes.size();
	verified.duration_seconds = duration;
	verified.frame_rate = frame_rate;
	verified.has_audio = probes.front().has_audio;
	verified.audio_sample_rate = probes.front().audio_sample_rate;
	verified.audio_channels = probes.front().audio_channels;

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
	if (verification != nullptr)
		*verification = verified;
	return true;
}

} // namespace openvolumetric::authoring
