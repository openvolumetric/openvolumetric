#include "OpenVolumetricAuthoringApi.h"

#include "AuthoringWorkflow.h"
#include "DracoMeshEncoder.h"
#include "VolumetricVideoPacker.h"

#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
thread_local std::string last_error;
thread_local std::string last_report;
thread_local std::string last_arguments;
}

int openvolumetric_authoring_get_preset(
	int preset,
	OpenVolumetricAuthoringSettings* output)
{
	if (output == nullptr || preset < 0 || preset > 2)
		return -1;
	const auto settings = openvolumetric::authoring::preset_settings(
		static_cast<openvolumetric::authoring::PlatformPreset>(preset));
	output->codec =
		settings.codec == openvolumetric::authoring::VideoCodec::HEVC ? 0 : 1;
	output->crf = settings.crf;
	output->video_keyframe_interval = settings.video_keyframe_interval;
	output->reference_frames = settings.reference_frames;
	output->disable_sao = settings.disable_sao ? 1 : 0;
	output->position_quantization = settings.position_quantization;
	output->normal_quantization = settings.normal_quantization;
	output->texture_quantization = settings.texture_quantization;
	output->draco_encode_speed = settings.draco_encode_speed;
	output->draco_decode_speed = settings.draco_decode_speed;
	return 1;
}

int openvolumetric_authoring_validate_sources(
	const char* image_directory,
	const char* geometry_directory)
{
	last_error.clear();
	if (image_directory == nullptr || geometry_directory == nullptr)
	{
		last_error = "Image and OBJ directories are required.";
		return -1;
	}
	openvolumetric::authoring::SourceSequenceInfo info;
	return openvolumetric::authoring::validate_source_sequences(
		std::filesystem::u8path(image_directory),
		std::filesystem::u8path(geometry_directory),
		info,
		last_error) ? 1 : -1;
}

const char* openvolumetric_authoring_build_ffmpeg_arguments(
	const char* image_pattern,
	const char* audio_path,
	const char* output_path,
	double frame_rate,
	int first_frame,
	int frame_count,
	const OpenVolumetricAuthoringSettings* settings)
{
	last_error.clear();
	last_arguments.clear();
	if (image_pattern == nullptr ||
		output_path == nullptr ||
		settings == nullptr ||
		frame_count < 0)
	{
		last_error = "FFmpeg request fields are invalid.";
		return nullptr;
	}
	openvolumetric::authoring::MediaEncodeRequest request;
	request.image_pattern = std::filesystem::u8path(image_pattern);
	if (audio_path != nullptr && audio_path[0] != '\0')
		request.audio_path = std::filesystem::u8path(audio_path);
	request.output_path = std::filesystem::u8path(output_path);
	request.frame_rate = frame_rate;
	request.first_frame = first_frame;
	request.frame_count = static_cast<std::size_t>(frame_count);
	request.settings.codec = settings->codec == 0
		? openvolumetric::authoring::VideoCodec::HEVC
		: openvolumetric::authoring::VideoCodec::H264;
	request.settings.crf = settings->crf;
	request.settings.video_keyframe_interval =
		settings->video_keyframe_interval;
	request.settings.reference_frames = settings->reference_frames;
	request.settings.disable_sao = settings->disable_sao != 0;
	request.settings.position_quantization =
		settings->position_quantization;
	request.settings.normal_quantization = settings->normal_quantization;
	request.settings.texture_quantization =
		settings->texture_quantization;
	request.settings.draco_encode_speed = settings->draco_encode_speed;
	request.settings.draco_decode_speed = settings->draco_decode_speed;

	std::vector<std::string> arguments;
	if (!openvolumetric::authoring::build_ffmpeg_arguments(
		request, arguments, last_error))
	{
		return nullptr;
	}
	for (const std::string& argument : arguments)
	{
		if (argument.find('\n') != std::string::npos ||
			argument.find('\r') != std::string::npos)
		{
			last_error = "FFmpeg paths cannot contain newline characters.";
			last_arguments.clear();
			return nullptr;
		}
		if (!last_arguments.empty())
			last_arguments.push_back('\n');
		last_arguments += argument;
	}
	return last_arguments.c_str();
}

int openvolumetric_authoring_encode_obj(
	const char* input_path,
	const char* output_path,
	int position_quantization,
	int normal_quantization,
	int texture_quantization,
	int encode_speed,
	int decode_speed,
	int enable_topology_compression)
{
	last_error.clear();
	last_report.clear();
	if (input_path == nullptr || output_path == nullptr)
	{
		last_error = "OBJ input and Draco output paths are required.";
		return -1;
	}

	try
	{
		openvolumetric::authoring::DracoEncodeOptions options;
		options.position_quantization = position_quantization;
		options.normal_quantization = normal_quantization;
		options.texture_quantization = texture_quantization;
		options.encode_speed = encode_speed;
		options.decode_speed = decode_speed;
		// Authoring may later reuse the decoded keyframe topology, so every
		// temporary full mesh must retain canonical vertex/index ordering.
		options.preserve_point_order = enable_topology_compression != 0;
		return openvolumetric::authoring::encode_obj_to_draco(
			std::filesystem::u8path(input_path),
			std::filesystem::u8path(output_path),
			options,
			last_error) ? 1 : -1;
	}
	catch (const std::exception& exception)
	{
		last_error = exception.what();
		return -1;
	}
	catch (...)
	{
		last_error = "Unknown native Draco encoding error.";
		return -1;
	}
}

int openvolumetric_authoring_pack(
	const char* media_path,
	const char* geometry_directory,
	const char* source_geometry_directory,
	const char* output_path,
	int position_quantization,
	int normal_quantization,
	int texture_quantization,
	int encode_speed,
	int decode_speed,
	int enable_topology_compression,
	int maximum_geometry_keyframe_interval)
{
	last_error.clear();
	last_report.clear();
	if (media_path == nullptr ||
		geometry_directory == nullptr ||
		source_geometry_directory == nullptr ||
		output_path == nullptr)
	{
		last_error = "Media, geometry, and output paths are required.";
		return -1;
	}
	if (maximum_geometry_keyframe_interval < 0)
	{
		last_error =
			"Maximum geometry keyframe interval cannot be negative.";
		return -1;
	}

	try
	{
		openvolumetric::authoring::PackOptions options;
		options.media_path = media_path;
		options.geometry_directory = geometry_directory;
		options.source_geometry_directory = source_geometry_directory;
		options.output_path = output_path;
		options.enable_topology_compression =
			enable_topology_compression != 0;
		options.maximum_geometry_keyframe_interval =
			static_cast<std::uint32_t>(
				maximum_geometry_keyframe_interval);
		options.draco_options.position_quantization =
			position_quantization;
		options.draco_options.normal_quantization =
			normal_quantization;
		options.draco_options.texture_quantization =
			texture_quantization;
		options.draco_options.encode_speed = encode_speed;
		options.draco_options.decode_speed = decode_speed;
		openvolumetric::authoring::PackStatistics statistics;
		if (!openvolumetric::authoring::pack_openvolumetric(
			options, &statistics))
		{
			last_error =
				"Packaging or output verification failed. Check the encoder log.";
			return -1;
		}
		const double reduction =
			statistics.independent_payload_bytes == 0
				? 0.0
				: 100.0 * (1.0 -
					static_cast<double>(statistics.authored_payload_bytes) /
					static_cast<double>(
						statistics.independent_payload_bytes));
		std::ostringstream report;
		report << "Geometry statistics: "
			<< statistics.frame_count << " frames, "
			<< statistics.independent_mesh_count
			<< " independent mesh keyframes, "
			<< statistics.position_update_count
			<< " position updates.\n"
			<< "Geometry payload: "
			<< statistics.authored_payload_bytes << " bytes + "
			<< statistics.packet_header_bytes
			<< " bytes packet headers; independent baseline "
			<< statistics.independent_payload_bytes << " bytes; "
			<< std::fixed << std::setprecision(2)
			<< reduction << "% payload reduction.";
		last_report = report.str();
		return 1;
	}
	catch (const std::exception& exception)
	{
		last_error = exception.what();
		return -1;
	}
	catch (...)
	{
		last_error = "Unknown native authoring error.";
		return -1;
	}
}

const char* openvolumetric_authoring_last_error()
{
	return last_error.c_str();
}

const char* openvolumetric_authoring_last_report()
{
	return last_report.c_str();
}
