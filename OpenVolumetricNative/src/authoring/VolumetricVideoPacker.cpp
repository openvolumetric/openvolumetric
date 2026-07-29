#include "VolumetricVideoPacker.h"

#include "DracoMeshEncoder.h"
#include "DracoPointCloudEncoder.h"
#include "TopologyAnalyzer.h"

#include <GeometryPacket.h>

#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace openvolumetric::authoring
{
namespace
{

namespace fs = std::filesystem;

/// Packs four ASCII bytes into the integer representation used by FFmpeg tags.
constexpr std::uint32_t make_tag(char a, char b, char c, char d)
{
	return
		static_cast<std::uint32_t>(a) |
		(static_cast<std::uint32_t>(b) << 8) |
		(static_cast<std::uint32_t>(c) << 16) |
		(static_cast<std::uint32_t>(d) << 24);
}

constexpr std::uint32_t kFFmpegBinaryMetadataTag =
	make_tag('g', 'p', 'm', 'd');
constexpr std::uint32_t kVolumetricGeometryTag =
	make_tag('v', 'v', 'g', 'e');

struct GeometryInput
{
	std::uint32_t frame_number;
	fs::path path;
	openvolumetric::GeometryPacket packet;
};

struct VideoSampleTiming
{
	std::int64_t pts;
	std::int64_t duration;
};

/// Converts one FFmpeg error code into readable text.
std::string ffmpeg_error(int error)
{
	std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
	av_strerror(error, buffer.data(), buffer.size());
	return buffer.data();
}

/// Reads an entire geometry payload while rejecting empty or unreadable files.
bool read_file(const fs::path& path, std::vector<std::uint8_t>& output)
{
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input)
	{
		return false;
	}

	const std::streamsize size = input.tellg();
	if (size <= 0 ||
		static_cast<std::uint64_t>(size) >
			static_cast<std::uint64_t>(
				std::numeric_limits<std::uint32_t>::max()))
	{
		return false;
	}

	output.resize(static_cast<std::size_t>(size));
	input.seekg(0);
	return static_cast<bool>(input.read(
		reinterpret_cast<char*>(output.data()), size));
}

/// Discovers a contiguous, numerically named Draco sequence.
bool discover_geometry(
	const fs::path& directory,
	std::vector<GeometryInput>& geometry)
{
	if (!fs::is_directory(directory))
	{
		std::cerr << "Geometry directory does not exist: " << directory << '\n';
		return false;
	}

	for (const fs::directory_entry& entry : fs::directory_iterator(directory))
	{
		if (!entry.is_regular_file() || entry.path().extension() != ".drc")
		{
			continue;
		}

		const std::string stem = entry.path().stem().string();
		if (stem.empty() ||
			!std::all_of(stem.begin(), stem.end(), [](unsigned char value)
			{
				return value >= '0' && value <= '9';
			}))
		{
			std::cerr << "Ignoring non-numbered Draco file: "
				<< entry.path() << '\n';
			continue;
		}

		const unsigned long parsed = std::stoul(stem);
		if (parsed > std::numeric_limits<std::uint32_t>::max())
		{
			std::cerr << "Geometry frame number is too large: "
				<< entry.path() << '\n';
			return false;
		}

		geometry.push_back({
			static_cast<std::uint32_t>(parsed),
			entry.path(),
			{}
		});
	}

	std::sort(
		geometry.begin(),
		geometry.end(),
		[](const GeometryInput& left, const GeometryInput& right)
		{
			return left.frame_number < right.frame_number;
		});

	if (geometry.empty())
	{
		std::cerr << "No numbered .drc files found in " << directory << '\n';
		return false;
	}

	for (std::size_t i = 1; i < geometry.size(); ++i)
	{
		if (geometry[i - 1].frame_number == geometry[i].frame_number)
		{
			std::cerr << "Duplicate geometry frame number: "
				<< geometry[i].frame_number << '\n';
			return false;
		}
	}

	return true;
}

/// Builds one current-format packet for every source frame. Compression mode
/// selects whether matching frames become position updates or remain
/// independently decodable Draco meshes.
bool validate_reused_keyframe(
	const GeometryInput& input,
	const CanonicalMesh& canonical,
	const fs::path& obj_path)
{
	draco::DecoderBuffer buffer;
	buffer.Init(
		reinterpret_cast<const char*>(input.packet.payload.data()),
		input.packet.payload.size());
	draco::Decoder decoder;
	auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
	if (!decoded.ok())
	{
		std::cerr << "Could not validate reused Draco keyframe: "
			<< decoded.status().error_msg_string() << '\n';
		return false;
	}
	const std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();
	if (!mesh ||
		static_cast<std::size_t>(mesh->num_points()) !=
			canonical.vertex_count() ||
		static_cast<std::size_t>(mesh->num_faces()) !=
			canonical.triangle_count())
	{
		std::cerr << "Reused Draco keyframe geometry counts differ from OBJ "
			<< obj_path << '\n';
		return false;
	}
	for (draco::FaceIndex face(0); face < mesh->num_faces(); ++face)
	{
		const auto decoded_face = mesh->face(face);
		for (int corner = 0; corner < 3; ++corner)
		{
			const std::size_t index =
				static_cast<std::size_t>(face.value()) * 3 + corner;
			if (static_cast<std::uint32_t>(
					decoded_face[corner].value()) !=
				canonical.triangle_indices[index])
			{
				std::cerr
					<< "Reused Draco keyframe does not preserve canonical "
					   "vertex/index order: "
					<< input.path << '\n';
				return false;
			}
		}
	}
	return true;
}

/// Records the point and face counts that the runtime will actually see after
/// normal Draco mesh decoding. EdgeBreaker may split OBJ position vertices at
/// UV or normal seams, so canonical OBJ counts are not valid packet metadata
/// for an independently decoded mesh.
bool update_decoded_mesh_counts(
	GeometryInput& input,
	const fs::path& obj_path)
{
	draco::DecoderBuffer buffer;
	buffer.Init(
		reinterpret_cast<const char*>(input.packet.payload.data()),
		input.packet.payload.size());
	draco::Decoder decoder;
	auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
	if (!decoded.ok())
	{
		std::cerr << "Could not validate independent Draco mesh "
			<< obj_path << ": "
			<< decoded.status().error_msg_string() << '\n';
		return false;
	}
	const std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();
	if (!mesh || mesh->num_points() <= 0 || mesh->num_faces() <= 0 ||
		static_cast<std::uint64_t>(mesh->num_points()) >
			std::numeric_limits<std::uint32_t>::max() ||
		static_cast<std::uint64_t>(mesh->num_faces()) >
			std::numeric_limits<std::uint32_t>::max())
	{
		std::cerr << "Independent Draco mesh has invalid geometry counts: "
			<< obj_path << '\n';
		return false;
	}
	input.packet.vertex_count =
		static_cast<std::uint32_t>(mesh->num_points());
	input.packet.triangle_count =
		static_cast<std::uint32_t>(mesh->num_faces());
	return true;
}

bool prepare_geometry_packets(
	const PackOptions& options,
	std::vector<GeometryInput>& geometry,
	PackStatistics& statistics)
{
	if (!fs::is_directory(options.source_geometry_directory))
	{
		std::cerr << "OBJ geometry directory does not exist: "
			<< options.source_geometry_directory << '\n';
		return false;
	}

	TopologyOptions topology_options;
	CanonicalMesh previous;
	bool has_previous = false;
	std::uint32_t active_keyframe = 0;
	GeometryInput* active_keyframe_input = nullptr;
	fs::path active_keyframe_obj;
	bool active_keyframe_has_updates = false;
	bool active_keyframe_validated = false;
	std::uint32_t active_window_length = 0;
	std::size_t keyframe_count = 0;

	// A singleton topology does not require stable decoded point ordering.
	// Re-encode it with Draco's normal mesh method, which is substantially
	// smaller and faster to decode than the sequential method reserved for
	// keyframes referenced by position-only updates.
	auto finish_active_keyframe = [&]() -> bool
	{
		if (active_keyframe_input == nullptr ||
			active_keyframe_has_updates)
		{
			return true;
		}
		DracoEncodeOptions encode_options = options.draco_options;
		encode_options.preserve_point_order = false;
		std::string encode_error;
		if (!encode_obj_to_draco(
			active_keyframe_obj,
			encode_options,
			active_keyframe_input->packet.payload,
			encode_error))
		{
			std::cerr << "Could not encode independent mesh "
				<< active_keyframe_obj << ": " << encode_error << '\n';
			return false;
		}
		return update_decoded_mesh_counts(
			*active_keyframe_input, active_keyframe_obj);
	};

	for (GeometryInput& input : geometry)
	{
		const fs::path obj_path =
			options.source_geometry_directory /
			(input.path.stem().string() + ".obj");
		CanonicalMesh current;
		std::string error;
		if (!load_canonical_obj(
			obj_path, topology_options, current, error))
		{
			std::cerr << "Could not analyse OBJ frame "
				<< obj_path << ": " << error << '\n';
			return false;
		}
		if (current.vertex_count() >
				std::numeric_limits<std::uint32_t>::max() ||
			current.triangle_count() >
				std::numeric_limits<std::uint32_t>::max())
		{
			std::cerr << "Geometry counts exceed the packet format limits\n";
			return false;
		}

		const bool keyframe =
			!options.enable_topology_compression ||
			!has_previous ||
			!topology_matches(previous, current) ||
			(options.maximum_geometry_keyframe_interval > 0 &&
				active_window_length >=
					options.maximum_geometry_keyframe_interval);
		input.packet.version = openvolumetric::kGeometryPacketVersion;
		input.packet.frame_number = input.frame_number;
		input.packet.topology_id = current.topology_id;
		input.packet.vertex_count =
			static_cast<std::uint32_t>(current.vertex_count());
		input.packet.triangle_count =
			static_cast<std::uint32_t>(current.triangle_count());
		if (keyframe)
		{
			if (!finish_active_keyframe())
				return false;
			active_keyframe = input.frame_number;
			active_keyframe_input = &input;
			active_keyframe_obj = obj_path;
			active_keyframe_has_updates = false;
			active_keyframe_validated = false;
			active_window_length = 1;
			++keyframe_count;
			input.packet.flags = openvolumetric::kGeometryPacketKeyframe;
			input.packet.coding_mode =
				openvolumetric::GeometryCodingMode::IndependentMesh;
			input.packet.payload_codec =
				openvolumetric::GeometryPayloadCodec::DracoMesh;
		}
		else
		{
			++active_window_length;
			if (!active_keyframe_has_updates)
			{
				if (active_keyframe_input == nullptr ||
					!read_file(
						active_keyframe_input->path,
						active_keyframe_input->packet.payload))
				{
					std::cerr << "Failed to read order-preserving Draco "
						"keyframe\n";
					return false;
				}
				active_keyframe_has_updates = true;
			}
			if (!active_keyframe_validated)
			{
				if (active_keyframe_input == nullptr ||
					!validate_reused_keyframe(
						*active_keyframe_input, current, obj_path))
				{
					return false;
				}
				active_keyframe_validated = true;
			}
			input.packet.flags = 0;
			input.packet.coding_mode =
				openvolumetric::GeometryCodingMode::PositionUpdate;
			input.packet.payload_codec =
				openvolumetric::GeometryPayloadCodec::DracoPointCloud;
			if (!encode_positions_to_draco_point_cloud(
				current.positions, 14, 5, 5, input.packet.payload, error))
			{
				std::cerr << "Could not encode position update "
					<< obj_path << ": " << error << '\n';
				return false;
			}
		}
		input.packet.keyframe_frame_number = active_keyframe;
		previous = std::move(current);
		has_previous = true;
	}
	if (!finish_active_keyframe())
		return false;

	for (const GeometryInput& input : geometry)
	{
		statistics.authored_payload_bytes +=
			static_cast<std::uint64_t>(input.packet.payload.size());
		// The optimized independent packets are the meaningful no-temporal-
		// reuse baseline, including singleton topology groups.
		if (input.packet.coding_mode ==
			openvolumetric::GeometryCodingMode::IndependentMesh)
		{
			statistics.independent_payload_bytes +=
				static_cast<std::uint64_t>(input.packet.payload.size());
		}
		else
		{
			statistics.independent_payload_bytes +=
				static_cast<std::uint64_t>(fs::file_size(input.path));
		}
	}

	statistics.frame_count = geometry.size();
	statistics.independent_mesh_count = keyframe_count;
	statistics.position_update_count = geometry.size() - keyframe_count;
	statistics.packet_header_bytes =
		static_cast<std::uint64_t>(geometry.size()) *
		static_cast<std::uint64_t>(
			openvolumetric::kGeometryPacketHeaderSize);
	std::cout << "Authored " << keyframe_count
		<< " independent meshes and "
		<< (geometry.size() - keyframe_count)
		<< " position-only updates\n";
	return true;
}

/// Extracts one exact PTS/duration pair per source video frame.
bool collect_video_timing(
	AVFormatContext* input,
	int video_stream_index,
	std::vector<VideoSampleTiming>& timing)
{
	AVPacket* packet = av_packet_alloc();
	if (packet == nullptr)
		return false;

	int result = 0;
	while ((result = av_read_frame(input, packet)) >= 0)
	{
		if (packet->stream_index == video_stream_index)
		{
			const std::int64_t pts =
				packet->pts == AV_NOPTS_VALUE ? packet->dts : packet->pts;
			if (pts == AV_NOPTS_VALUE)
			{
				std::cerr << "Video packet has no usable timestamp\n";
				av_packet_free(&packet);
				return false;
			}
			timing.push_back({pts, packet->duration});
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
	if (result != AVERROR_EOF || timing.empty())
	{
		std::cerr << "Could not collect source video sample timestamps\n";
		return false;
	}

	std::sort(timing.begin(), timing.end(), [](const auto& left, const auto& right)
	{
		return left.pts < right.pts;
	});
	for (std::size_t index = 1; index < timing.size(); ++index)
	{
		if (timing[index].pts <= timing[index - 1].pts)
		{
			std::cerr << "Video presentation timestamps are not unique and monotonic\n";
			return false;
		}
		if (timing[index - 1].duration <= 0)
			timing[index - 1].duration =
				timing[index].pts - timing[index - 1].pts;
	}
	if (timing.back().duration <= 0)
	{
		timing.back().duration = timing.size() > 1
			? timing[timing.size() - 2].duration
			: 1;
	}

	result = avformat_seek_file(
		input, -1, std::numeric_limits<std::int64_t>::min(), 0,
		std::numeric_limits<std::int64_t>::max(), 0);
	if (result < 0)
	{
		std::cerr << "Could not rewind source media after timing probe: "
			<< ffmpeg_error(result) << '\n';
		return false;
	}
	avformat_flush(input);
	return true;
}

/// Wraps and writes one Draco payload as a timed VVGF geometry sample.
bool write_geometry_sample(
	AVFormatContext* output,
	AVStream* stream,
	AVRational source_time_base,
	const VideoSampleTiming& timing,
	const GeometryInput& input)
{
	const std::vector<std::uint8_t> bytes =
		openvolumetric::serialize_geometry_packet(input.packet);
	if (bytes.empty() ||
		bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
	{
		std::cerr << "Failed to serialize geometry frame: " << input.path << '\n';
		return false;
	}

	AVPacket* packet = av_packet_alloc();
	if (packet == nullptr)
	{
		return false;
	}

	int result = av_new_packet(packet, static_cast<int>(bytes.size()));
	if (result >= 0)
	{
		std::copy(bytes.begin(), bytes.end(), packet->data);
		packet->stream_index = stream->index;
		packet->pts = timing.pts;
		packet->dts = timing.pts;
		packet->duration = timing.duration;
		packet->flags =
			(input.packet.flags & openvolumetric::kGeometryPacketKeyframe) != 0
				? AV_PKT_FLAG_KEY
				: 0;
		av_packet_rescale_ts(packet, source_time_base, stream->time_base);
		result = av_interleaved_write_frame(output, packet);
	}

	av_packet_free(&packet);
	if (result < 0)
	{
		std::cerr << "Failed to write geometry frame "
			<< input.frame_number << ": " << ffmpeg_error(result) << '\n';
		return false;
	}
	return true;
}

/// Copies media streams and interleaves a newly created geometry stream.
bool mux_file(
	const PackOptions& options,
	const std::vector<GeometryInput>& geometry,
	const fs::path& temporary_path)
{
	AVFormatContext* input = nullptr;
	AVFormatContext* output = nullptr;
	AVPacket* packet = nullptr;
	bool output_open = false;
	bool header_written = false;
	bool success = false;

	int result = avformat_open_input(
		&input, options.media_path.string().c_str(), nullptr, nullptr);
	if (result < 0)
	{
		std::cerr << "Failed to open input media: "
			<< ffmpeg_error(result) << '\n';
		goto cleanup;
	}

	result = avformat_find_stream_info(input, nullptr);
	if (result < 0)
	{
		std::cerr << "Failed to inspect input media: "
			<< ffmpeg_error(result) << '\n';
		goto cleanup;
	}

	{
		const int video_index = av_find_best_stream(
			input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (video_index < 0)
		{
			std::cerr << "Input media has no video stream\n";
			goto cleanup;
		}
		std::vector<VideoSampleTiming> video_timing;
		if (!collect_video_timing(input, video_index, video_timing))
			goto cleanup;
		if (video_timing.size() != geometry.size())
		{
			std::cerr << "Geometry/video frame count mismatch: "
				<< geometry.size() << " geometry frames and "
				<< video_timing.size() << " timestamped video samples\n";
			goto cleanup;
		}
		std::cout << "Using " << video_timing.size()
			<< " source video sample timestamps for geometry timing\n";

		const AVRational geometry_time_base =
			input->streams[video_index]->time_base;

		result = avformat_alloc_output_context2(
			&output, nullptr, "mp4", temporary_path.string().c_str());
		if (result < 0 || output == nullptr)
		{
			std::cerr << "Failed to create MP4 output: "
				<< ffmpeg_error(result) << '\n';
			goto cleanup;
		}

		std::vector<AVRational> input_time_bases;
		input_time_bases.reserve(input->nb_streams);
		for (unsigned int i = 0; i < input->nb_streams; ++i)
		{
			AVStream* output_stream = avformat_new_stream(output, nullptr);
			if (output_stream == nullptr)
			{
				std::cerr << "Failed to create output media stream\n";
				goto cleanup;
			}

			result = avcodec_parameters_copy(
				output_stream->codecpar, input->streams[i]->codecpar);
			if (result < 0)
			{
				std::cerr << "Failed to copy stream parameters: "
					<< ffmpeg_error(result) << '\n';
				goto cleanup;
			}
			output_stream->codecpar->codec_tag = 0;
			output_stream->time_base = input->streams[i]->time_base;
			input_time_bases.push_back(input->streams[i]->time_base);
		}

		AVStream* geometry_stream = avformat_new_stream(output, nullptr);
		if (geometry_stream == nullptr)
		{
			std::cerr << "Failed to create geometry metadata stream\n";
			goto cleanup;
		}
		geometry_stream->codecpar->codec_type = AVMEDIA_TYPE_DATA;
		geometry_stream->codecpar->codec_id = AV_CODEC_ID_BIN_DATA;
		geometry_stream->codecpar->codec_tag = kFFmpegBinaryMetadataTag;
		geometry_stream->time_base = geometry_time_base;
		av_dict_set(
			&geometry_stream->metadata,
			"handler_name",
			"Volumetric Geometry",
			0);
		av_dict_set(
			&geometry_stream->metadata,
			"title",
			"Draco Geometry",
			0);

		if (!(output->oformat->flags & AVFMT_NOFILE))
		{
			result = avio_open(
				&output->pb,
				temporary_path.string().c_str(),
				AVIO_FLAG_WRITE);
			if (result < 0)
			{
				std::cerr << "Failed to open output file: "
					<< ffmpeg_error(result) << '\n';
				goto cleanup;
			}
			output_open = true;
		}

		result = avformat_write_header(output, nullptr);
		if (result < 0)
		{
			std::cerr << "Failed to write MP4 header: "
				<< ffmpeg_error(result) << '\n';
			goto cleanup;
		}
		header_written = true;

		packet = av_packet_alloc();
		if (packet == nullptr)
		{
			goto cleanup;
		}

		std::size_t next_geometry = 0;
		while ((result = av_read_frame(input, packet)) >= 0)
		{
			const int input_stream_index = packet->stream_index;
			const AVStream* input_stream = input->streams[input_stream_index];
			const std::int64_t media_timestamp =
				packet->dts == AV_NOPTS_VALUE ? packet->pts : packet->dts;

			while (next_geometry < geometry.size() &&
				media_timestamp != AV_NOPTS_VALUE &&
				av_compare_ts(
					video_timing[next_geometry].pts,
					geometry_time_base,
					media_timestamp,
					input_stream->time_base) <= 0)
			{
				if (!write_geometry_sample(
					output,
					geometry_stream,
					geometry_time_base,
					video_timing[next_geometry],
					geometry[next_geometry]))
				{
					goto cleanup;
				}
				++next_geometry;
			}

			packet->stream_index = input_stream_index;
			av_packet_rescale_ts(
				packet,
				input_time_bases[input_stream_index],
				output->streams[input_stream_index]->time_base);
			packet->pos = -1;
			result = av_interleaved_write_frame(output, packet);
			av_packet_unref(packet);
			if (result < 0)
			{
				std::cerr << "Failed to copy media packet: "
					<< ffmpeg_error(result) << '\n';
				goto cleanup;
			}
		}

		if (result != AVERROR_EOF)
		{
			std::cerr << "Failed while reading input media: "
				<< ffmpeg_error(result) << '\n';
			goto cleanup;
		}

		while (next_geometry < geometry.size())
		{
			if (!write_geometry_sample(
				output,
				geometry_stream,
				geometry_time_base,
				video_timing[next_geometry],
				geometry[next_geometry]))
			{
				goto cleanup;
			}
			++next_geometry;
		}

		result = av_write_trailer(output);
		header_written = false;
		if (result < 0)
		{
			std::cerr << "Failed to finish MP4 output: "
				<< ffmpeg_error(result) << '\n';
			goto cleanup;
		}
	}

	success = true;

cleanup:
	if (header_written && output != nullptr)
	{
		av_write_trailer(output);
	}
	av_packet_free(&packet);
	if (output_open && output != nullptr)
	{
		avio_closep(&output->pb);
	}
	avformat_free_context(output);
	if (input != nullptr)
	{
		avformat_close_input(&input);
	}
	return success;
}

/// Reads an unsigned big-endian MP4 box field.
std::uint32_t read_be32(const std::uint8_t* value)
{
	return
		(static_cast<std::uint32_t>(value[0]) << 24) |
		(static_cast<std::uint32_t>(value[1]) << 16) |
		(static_cast<std::uint32_t>(value[2]) << 8) |
		static_cast<std::uint32_t>(value[3]);
}

/// Rewrites FFmpeg's generic binary-data sample entry to the OpenVolumetric vvge tag.
bool replace_geometry_sample_entry(const fs::path& path)
{
	std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
	if (!file)
	{
		return false;
	}

	file.seekg(0, std::ios::end);
	const std::uint64_t file_size =
		static_cast<std::uint64_t>(file.tellg());
	std::uint64_t offset = 0;
	std::uint64_t moov_offset = 0;
	std::uint64_t moov_size = 0;

	while (offset + 8 <= file_size)
	{
		std::array<std::uint8_t, 8> header{};
		file.seekg(static_cast<std::streamoff>(offset));
		if (!file.read(reinterpret_cast<char*>(header.data()), header.size()))
		{
			return false;
		}

		std::uint64_t box_size = read_be32(header.data());
		if (box_size == 1)
		{
			std::array<std::uint8_t, 8> extended{};
			if (!file.read(
				reinterpret_cast<char*>(extended.data()), extended.size()))
			{
				return false;
			}
			box_size =
				(static_cast<std::uint64_t>(read_be32(extended.data())) << 32) |
				read_be32(extended.data() + 4);
		}
		else if (box_size == 0)
		{
			box_size = file_size - offset;
		}

		if (box_size < 8 || offset + box_size > file_size)
		{
			return false;
		}

		if (header[4] == 'm' && header[5] == 'o' &&
			header[6] == 'o' && header[7] == 'v')
		{
			moov_offset = offset;
			moov_size = box_size;
			break;
		}
		offset += box_size;
	}

	if (moov_size == 0 ||
		moov_size > static_cast<std::uint64_t>(
			std::numeric_limits<std::size_t>::max()))
	{
		return false;
	}

	std::vector<std::uint8_t> moov(static_cast<std::size_t>(moov_size));
	file.seekg(static_cast<std::streamoff>(moov_offset));
	if (!file.read(
		reinterpret_cast<char*>(moov.data()),
		static_cast<std::streamsize>(moov.size())))
	{
		return false;
	}

	std::size_t replacements = 0;
	for (std::size_t i = 0; i + 4 <= moov.size(); ++i)
	{
		if (moov[i] == 'g' && moov[i + 1] == 'p' &&
			moov[i + 2] == 'm' && moov[i + 3] == 'd')
		{
			moov[i] = 'v';
			moov[i + 1] = 'v';
			moov[i + 2] = 'g';
			moov[i + 3] = 'e';
			++replacements;
			i += 3;
		}
	}

	if (replacements < 2)
	{
		std::cerr << "Expected MP4 geometry declarations were not found\n";
		return false;
	}

	file.seekp(static_cast<std::streamoff>(moov_offset));
	if (!file.write(
		reinterpret_cast<const char*>(moov.data()),
		static_cast<std::streamsize>(moov.size())))
	{
		return false;
	}
	file.flush();

	std::cout << "Converted " << replacements
		<< " MP4 metadata declarations to vvge\n";
	return true;
}

/// Reopens the output and verifies tracks, timestamps, payloads, and seeking.
bool verify_file(
	const fs::path& path,
	const std::vector<GeometryInput>& geometry)
{
	AVFormatContext* input = nullptr;
	AVPacket* packet = nullptr;
	bool success = false;
	int result = avformat_open_input(
		&input, path.string().c_str(), nullptr, nullptr);
	if (result < 0)
	{
		std::cerr << "Failed to reopen output: "
			<< ffmpeg_error(result) << '\n';
		goto cleanup;
	}

	result = avformat_find_stream_info(input, nullptr);
	if (result < 0)
	{
		std::cerr << "Failed to inspect output: "
			<< ffmpeg_error(result) << '\n';
		goto cleanup;
	}

	{
		int geometry_stream_index = -1;
		for (unsigned int i = 0; i < input->nb_streams; ++i)
		{
			const AVCodecParameters* parameters = input->streams[i]->codecpar;
			if (parameters->codec_type == AVMEDIA_TYPE_DATA &&
				parameters->codec_tag == kVolumetricGeometryTag)
			{
				geometry_stream_index = static_cast<int>(i);
				break;
			}
		}
		if (geometry_stream_index < 0)
		{
			std::cerr << "Reopened MP4 does not expose a vvge data stream\n";
			goto cleanup;
		}
		const int video_stream_index = av_find_best_stream(
			input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (video_stream_index < 0)
		{
			std::cerr << "Reopened MP4 has no video stream\n";
			goto cleanup;
		}
		std::vector<VideoSampleTiming> video_timing;
		if (!collect_video_timing(
				input, video_stream_index, video_timing))
		{
			goto cleanup;
		}
		if (video_timing.size() != geometry.size())
		{
			std::cerr << "Verified video/geometry sample count mismatch\n";
			goto cleanup;
		}
		const AVRational video_time_base =
			input->streams[video_stream_index]->time_base;

		packet = av_packet_alloc();
		if (packet == nullptr)
		{
			goto cleanup;
		}

		std::size_t geometry_index = 0;
		std::int64_t previous_pts = AV_NOPTS_VALUE;
		while ((result = av_read_frame(input, packet)) >= 0)
		{
			if (packet->stream_index == geometry_stream_index)
			{
				if (geometry_index >= geometry.size())
				{
					std::cerr << "Output contains extra geometry samples\n";
					goto cleanup;
				}

				openvolumetric::GeometryPacket decoded;
				if (!openvolumetric::parse_geometry_packet(
					packet->data,
					static_cast<std::size_t>(packet->size),
					decoded))
				{
					std::cerr << "Invalid geometry packet at sample "
						<< geometry_index << '\n';
					goto cleanup;
				}
				if (decoded.frame_number !=
					geometry[geometry_index].frame_number)
				{
					std::cerr << "Geometry frame order mismatch\n";
					goto cleanup;
				}

				const openvolumetric::GeometryPacket& expected =
					geometry[geometry_index].packet;
				if (decoded.version != expected.version ||
					decoded.flags != expected.flags ||
					decoded.coding_mode != expected.coding_mode ||
					decoded.payload_codec != expected.payload_codec ||
					decoded.topology_id != expected.topology_id ||
					decoded.keyframe_frame_number !=
						expected.keyframe_frame_number ||
					decoded.vertex_count != expected.vertex_count ||
					decoded.triangle_count != expected.triangle_count ||
					decoded.payload != expected.payload)
				{
					std::cerr << "Geometry payload mismatch for frame "
						<< decoded.frame_number << '\n';
					goto cleanup;
				}
				if (previous_pts != AV_NOPTS_VALUE &&
					packet->pts <= previous_pts)
				{
					std::cerr << "Geometry timestamps are not monotonic\n";
					goto cleanup;
				}
				const std::int64_t expected_pts = av_rescale_q(
					video_timing[geometry_index].pts,
					video_time_base,
					input->streams[geometry_stream_index]->time_base);
				const std::int64_t expected_duration = av_rescale_q(
					video_timing[geometry_index].duration,
					video_time_base,
					input->streams[geometry_stream_index]->time_base);
				if (packet->pts != expected_pts ||
					packet->duration != expected_duration)
				{
					std::cerr << "Geometry timing mismatch for frame "
						<< decoded.frame_number << '\n';
					goto cleanup;
				}
				previous_pts = packet->pts;
				++geometry_index;
			}
			av_packet_unref(packet);
		}

		if (result != AVERROR_EOF || geometry_index != geometry.size())
		{
			std::cerr << "Geometry sample count mismatch: expected "
				<< geometry.size() << ", read " << geometry_index << '\n';
			goto cleanup;
		}

		const GeometryInput& seek_input = geometry[geometry.size() / 2];
		const std::uint32_t expected_seek_frame =
			seek_input.packet.keyframe_frame_number;
		const auto seek_geometry_sample =
			[&](std::size_t timing_index,
				std::uint32_t expected_frame,
				std::uint32_t expected_keyframe) -> bool
		{
			const std::int64_t timestamp = av_rescale_q(
				video_timing[timing_index].pts,
				video_time_base,
				input->streams[geometry_stream_index]->time_base);
			int seek_result = av_seek_frame(
				input,
				geometry_stream_index,
				timestamp,
				AVSEEK_FLAG_BACKWARD);
			if (seek_result < 0)
			{
				std::cerr << "Geometry seek failed: "
					<< ffmpeg_error(seek_result) << '\n';
				return false;
			}
			avformat_flush(input);
			av_packet_unref(packet);
			while ((seek_result = av_read_frame(input, packet)) >= 0)
			{
				if (packet->stream_index == geometry_stream_index)
				{
					openvolumetric::GeometryPacket decoded;
					const bool valid =
						openvolumetric::parse_geometry_packet(
							packet->data,
							static_cast<std::size_t>(packet->size),
						decoded) &&
						decoded.frame_number == expected_frame &&
						decoded.keyframe_frame_number == expected_keyframe;
					if (!valid)
					{
						std::cerr << "Geometry seek dependency mismatch\n";
						return false;
					}
					return true;
				}
				av_packet_unref(packet);
			}
			std::cerr << "Geometry seek produced no geometry sample\n";
			return false;
		};

		if (!seek_geometry_sample(
			geometry.size() / 2,
			seek_input.frame_number,
			expected_seek_frame))
		{
			goto cleanup;
		}
		const auto keyframe_iterator = std::find_if(
			geometry.begin(),
			geometry.end(),
			[&](const GeometryInput& value)
			{
				return value.frame_number == expected_seek_frame;
			});
		if (keyframe_iterator == geometry.end() ||
			!seek_geometry_sample(
				static_cast<std::size_t>(
					std::distance(geometry.begin(), keyframe_iterator)),
				expected_seek_frame,
				expected_seek_frame))
		{
			goto cleanup;
		}

		std::cout << "Verified " << geometry_index
			<< " geometry samples and keyframe seek from frame "
			<< seek_input.frame_number << " to frame "
			<< expected_seek_frame << '\n';
	}

	success = true;

cleanup:
	av_packet_free(&packet);
	if (input != nullptr)
	{
		avformat_close_input(&input);
	}
	return success;
}

} // namespace

bool pack_openvolumetric(
	const PackOptions& options,
	PackStatistics* statistics)
{
	if (options.media_path.empty() ||
		options.geometry_directory.empty() ||
		options.output_path.empty())
	{
		std::cerr << "Media, geometry, and output paths are required\n";
		return false;
	}

	if (!fs::is_regular_file(options.media_path))
	{
		std::cerr << "Input media does not exist: "
			<< options.media_path << '\n';
		return false;
	}
	if (fs::exists(options.output_path))
	{
		std::cerr << "Refusing to overwrite existing output: "
			<< options.output_path << '\n';
		return false;
	}

	std::vector<GeometryInput> geometry;
	if (!discover_geometry(options.geometry_directory, geometry))
	{
		return false;
	}
	PackStatistics result_statistics;
	if (!prepare_geometry_packets(options, geometry, result_statistics))
	{
		return false;
	}

	fs::path temporary_path = options.output_path;
	temporary_path += ".authoring-tmp.mp4";
	std::error_code ignored;
	fs::remove(temporary_path, ignored);

	std::cout << "Packing " << geometry.size()
		<< " geometry samples into MP4\n";
	if (!mux_file(options, geometry, temporary_path) ||
		!replace_geometry_sample_entry(temporary_path) ||
		!verify_file(temporary_path, geometry))
	{
		fs::remove(temporary_path, ignored);
		return false;
	}

	fs::rename(temporary_path, options.output_path);
	if (statistics != nullptr)
		*statistics = result_statistics;
	std::cout << "Verified volumetric MP4: "
		<< options.output_path << '\n';
	return true;
}

} // namespace openvolumetric::authoring
