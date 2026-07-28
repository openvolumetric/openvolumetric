#include "OpenVolumetricAuthoringApi.h"

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
	int enable_topology_compression)
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

	try
	{
		openvolumetric::authoring::PackOptions options;
		options.media_path = media_path;
		options.geometry_directory = geometry_directory;
		options.source_geometry_directory = source_geometry_directory;
		options.output_path = output_path;
		options.enable_topology_compression =
			enable_topology_compression != 0;
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
