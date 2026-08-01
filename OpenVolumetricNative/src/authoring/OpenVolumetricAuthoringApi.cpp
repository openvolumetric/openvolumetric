#include "OpenVolumetricAuthoringApi.h"

#include "AdaptivePackage.h"
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
thread_local std::uint64_t last_geometry_payload_bytes = 0;

void copy_settings(
	const openvolumetric::authoring::EncodingSettings& settings,
	OpenVolumetricAuthoringSettings& output)
{
	output.codec =
		settings.codec == openvolumetric::authoring::VideoCodec::HEVC ? 0 : 1;
	output.crf = settings.crf;
	output.video_keyframe_interval = settings.video_keyframe_interval;
	output.reference_frames = settings.reference_frames;
	output.disable_sao = settings.disable_sao ? 1 : 0;
	output.position_quantization = settings.position_quantization;
	output.normal_quantization = settings.normal_quantization;
	output.texture_quantization = settings.texture_quantization;
	output.draco_encode_speed = settings.draco_encode_speed;
	output.draco_decode_speed = settings.draco_decode_speed;
	output.maximum_video_bitrate_kbps = settings.maximum_video_bitrate_kbps;
	output.video_buffer_size_kbps = settings.video_buffer_size_kbps;
	output.geometry_keyframe_interval = settings.geometry_keyframe_interval;
	output.fragment_duration_seconds = settings.fragment_duration_seconds;
}
}

int openvolumetric_authoring_get_preset(
	int preset,
	OpenVolumetricAuthoringSettings* output)
{
	if (output == nullptr || preset < 0 || preset > 3)
		return -1;
	const auto settings = openvolumetric::authoring::preset_settings(
		static_cast<openvolumetric::authoring::PlatformPreset>(preset));
	copy_settings(settings, *output);
	return 1;
}

int openvolumetric_authoring_get_adaptive_preset(
	int preset,
	int quality,
	int fragment_duration_seconds,
	OpenVolumetricAuthoringSettings* output)
{
	last_error.clear();
	if (output == nullptr || quality < 0 || quality > 1 ||
		preset < 0 || preset > 3)
	{
		last_error = "Adaptive preset request is invalid.";
		return -1;
	}
	std::vector<openvolumetric::authoring::AdaptiveLadderEntry> entries;
	if (!openvolumetric::authoring::adaptive_ladder_settings(
			static_cast<openvolumetric::authoring::PlatformPreset>(preset),
			fragment_duration_seconds,
			entries,
			last_error))
	{
		return -1;
	}
	copy_settings(entries[static_cast<std::size_t>(quality)].settings, *output);
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
	request.settings.maximum_video_bitrate_kbps =
		settings->maximum_video_bitrate_kbps;
	request.settings.video_buffer_size_kbps =
		settings->video_buffer_size_kbps;
	request.settings.geometry_keyframe_interval =
		settings->geometry_keyframe_interval;
	request.settings.fragment_duration_seconds =
		settings->fragment_duration_seconds;

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
	last_geometry_payload_bytes = 0;
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
	int maximum_geometry_keyframe_interval,
	int fragment_duration_seconds,
	int fragment_frame_interval)
{
	last_error.clear();
	last_report.clear();
	last_geometry_payload_bytes = 0;
	if (media_path == nullptr ||
		geometry_directory == nullptr ||
		source_geometry_directory == nullptr ||
		output_path == nullptr)
	{
		last_error = "Media, geometry, and output paths are required.";
		return -1;
	}
	if (maximum_geometry_keyframe_interval < 0 ||
		fragment_frame_interval < 0 ||
		!openvolumetric::authoring::is_supported_fragment_duration(
			fragment_duration_seconds) ||
		(fragment_duration_seconds > 0 && fragment_frame_interval == 0))
	{
		last_error = "Geometry keyframe or fragment settings are invalid.";
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
		options.fragment_duration_seconds =
			static_cast<std::uint32_t>(fragment_duration_seconds);
		options.fragment_frame_interval =
			static_cast<std::uint32_t>(fragment_frame_interval);
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
		if (statistics.fragment_count > 0)
		{
			report << "\nFragmented MP4: "
				<< statistics.fragment_count << " fragments at "
				<< fragment_duration_seconds << " seconds.";
		}
		last_report = report.str();
		last_geometry_payload_bytes =
			statistics.authored_payload_bytes + statistics.packet_header_bytes;
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

int openvolumetric_authoring_write_adaptive_manifest(
	const char* presentation_id,
	const char* manifest_path,
	double segment_duration_seconds,
	const char* low_id,
	const char* low_resource_path,
	int low_position_quantization_bits,
	unsigned long long low_geometry_payload_bytes,
	const char* high_id,
	const char* high_resource_path,
	int high_position_quantization_bits,
	unsigned long long high_geometry_payload_bytes,
	int temporal_compression)
{
	last_error.clear();
	last_report.clear();
	if (presentation_id == nullptr || manifest_path == nullptr ||
		low_id == nullptr || low_resource_path == nullptr ||
		high_id == nullptr || high_resource_path == nullptr ||
		low_position_quantization_bits <= 0 ||
		high_position_quantization_bits <= 0)
	{
		last_error = "Adaptive manifest fields are invalid.";
		return -1;
	}

	openvolumetric::authoring::AdaptivePackageOptions options;
	options.presentation_id = presentation_id;
	options.manifest_path = std::filesystem::u8path(manifest_path);
	options.segment_duration_seconds = segment_duration_seconds;
	const std::string compatibility_group =
		options.presentation_id + "-coupled-v1";
	options.representations = {
		{low_id,
		 std::filesystem::u8path(low_resource_path),
		 compatibility_group,
		 static_cast<std::uint32_t>(low_position_quantization_bits),
		 static_cast<std::uint64_t>(low_geometry_payload_bytes),
		 temporal_compression != 0},
		{high_id,
		 std::filesystem::u8path(high_resource_path),
		 compatibility_group,
		 static_cast<std::uint32_t>(high_position_quantization_bits),
		 static_cast<std::uint64_t>(high_geometry_payload_bytes),
		 temporal_compression != 0}};
	if (!openvolumetric::authoring::write_adaptive_package_manifest(
			options, last_error))
	{
		return -1;
	}
	last_report = "Adaptive manifest written with two aligned representations.";
	return 1;
}

const char* openvolumetric_authoring_last_error()
{
	return last_error.c_str();
}

const char* openvolumetric_authoring_last_report()
{
	return last_report.c_str();
}

unsigned long long openvolumetric_authoring_last_geometry_payload_bytes()
{
	return static_cast<unsigned long long>(last_geometry_payload_bytes);
}
