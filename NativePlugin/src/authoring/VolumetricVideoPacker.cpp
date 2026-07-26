#include "VolumetricVideoPacker.h"

#include <GeometryPacket.h>

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

namespace volumetric_video::authoring
{
namespace
{

namespace fs = std::filesystem;

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
};

std::string ffmpeg_error(int error)
{
	std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
	av_strerror(error, buffer.data(), buffer.size());
	return buffer.data();
}

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
			entry.path()
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

bool write_geometry_sample(
	AVFormatContext* output,
	AVStream* stream,
	AVRational source_time_base,
	std::size_t presentation_index,
	const GeometryInput& input)
{
	volumetric_video::GeometryPacket geometry_packet;
	geometry_packet.frame_number = input.frame_number;
	if (!read_file(input.path, geometry_packet.payload))
	{
		std::cerr << "Failed to read geometry frame: " << input.path << '\n';
		return false;
	}

	const std::vector<std::uint8_t> bytes =
		volumetric_video::serialize_geometry_packet(geometry_packet);
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
		packet->pts = static_cast<std::int64_t>(presentation_index);
		packet->dts = static_cast<std::int64_t>(presentation_index);
		packet->duration = 1;
		packet->flags = AV_PKT_FLAG_KEY;
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
		const std::int64_t video_frame_count =
			input->streams[video_index]->nb_frames;
		if (video_frame_count > 0 &&
			static_cast<std::uint64_t>(video_frame_count) != geometry.size())
		{
			std::cerr << "Geometry/video frame count mismatch: "
				<< geometry.size() << " geometry frames and "
				<< video_frame_count << " video frames\n";
			goto cleanup;
		}

		AVRational frame_rate = av_guess_frame_rate(
			input, input->streams[video_index], nullptr);
		if (frame_rate.num <= 0 || frame_rate.den <= 0)
		{
			std::cerr << "Input video has no usable frame rate\n";
			goto cleanup;
		}
		const AVRational geometry_time_base = av_inv_q(frame_rate);

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
					static_cast<std::int64_t>(next_geometry),
					geometry_time_base,
					media_timestamp,
					input_stream->time_base) <= 0)
			{
				if (!write_geometry_sample(
					output,
					geometry_stream,
					geometry_time_base,
					next_geometry,
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
				next_geometry,
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

std::uint32_t read_be32(const std::uint8_t* value)
{
	return
		(static_cast<std::uint32_t>(value[0]) << 24) |
		(static_cast<std::uint32_t>(value[1]) << 16) |
		(static_cast<std::uint32_t>(value[2]) << 8) |
		static_cast<std::uint32_t>(value[3]);
}

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
		AVRational frame_rate = av_guess_frame_rate(
			input, input->streams[video_stream_index], nullptr);
		if (frame_rate.num <= 0 || frame_rate.den <= 0)
		{
			std::cerr << "Could not recover the video frame rate\n";
			goto cleanup;
		}
		const AVRational geometry_time_base = av_inv_q(frame_rate);

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

				volumetric_video::GeometryPacket decoded;
				if (!volumetric_video::parse_geometry_packet(
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

				std::vector<std::uint8_t> original;
				if (!read_file(geometry[geometry_index].path, original) ||
					decoded.payload != original)
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
					static_cast<std::int64_t>(geometry_index),
					geometry_time_base,
					input->streams[geometry_stream_index]->time_base);
				const std::int64_t expected_duration = av_rescale_q(
					1,
					geometry_time_base,
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
		const std::int64_t seek_timestamp = av_rescale_q(
			static_cast<std::int64_t>(geometry.size() / 2),
			geometry_time_base,
			input->streams[geometry_stream_index]->time_base);
		result = av_seek_frame(
			input,
			geometry_stream_index,
			seek_timestamp,
			AVSEEK_FLAG_BACKWARD);
		if (result < 0)
		{
			std::cerr << "Geometry seek failed: "
				<< ffmpeg_error(result) << '\n';
			goto cleanup;
		}
		avformat_flush(input);

		bool found_seek_sample = false;
		av_packet_unref(packet);
		while ((result = av_read_frame(input, packet)) >= 0)
		{
			if (packet->stream_index == geometry_stream_index)
			{
				volumetric_video::GeometryPacket decoded;
				if (!volumetric_video::parse_geometry_packet(
					packet->data,
					static_cast<std::size_t>(packet->size),
					decoded) ||
					decoded.frame_number != seek_input.frame_number)
				{
					std::cerr << "Geometry seek returned frame "
						<< decoded.frame_number << " instead of "
						<< seek_input.frame_number << '\n';
					goto cleanup;
				}
				found_seek_sample = true;
				break;
			}
			av_packet_unref(packet);
		}
		if (!found_seek_sample)
		{
			std::cerr << "Geometry seek produced no geometry sample\n";
			goto cleanup;
		}

		std::cout << "Verified " << geometry_index
			<< " geometry samples and seek to frame "
			<< seek_input.frame_number << '\n';
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

bool pack_volumetric_video(const PackOptions& options)
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
	std::cout << "Verified volumetric MP4: "
		<< options.output_path << '\n';
	return true;
}

} // namespace volumetric_video::authoring
