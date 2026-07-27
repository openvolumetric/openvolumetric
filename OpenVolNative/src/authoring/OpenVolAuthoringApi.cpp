#include "OpenVolAuthoringApi.h"

#include "DracoMeshEncoder.h"
#include "VolumetricVideoPacker.h"

#include <exception>
#include <filesystem>
#include <string>

namespace
{
thread_local std::string last_error;
}

int openvol_authoring_encode_obj(
	const char* input_path,
	const char* output_path,
	int position_quantization,
	int normal_quantization,
	int texture_quantization,
	int encode_speed,
	int decode_speed)
{
	last_error.clear();
	if (input_path == nullptr || output_path == nullptr)
	{
		last_error = "OBJ input and Draco output paths are required.";
		return -1;
	}

	try
	{
		openvol::authoring::DracoEncodeOptions options;
		options.position_quantization = position_quantization;
		options.normal_quantization = normal_quantization;
		options.texture_quantization = texture_quantization;
		options.encode_speed = encode_speed;
		options.decode_speed = decode_speed;
		return openvol::authoring::encode_obj_to_draco(
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

int openvol_authoring_pack(
	const char* media_path,
	const char* geometry_directory,
	const char* output_path)
{
	last_error.clear();
	if (media_path == nullptr ||
		geometry_directory == nullptr ||
		output_path == nullptr)
	{
		last_error = "Media, geometry, and output paths are required.";
		return -1;
	}

	try
	{
		openvol::authoring::PackOptions options;
		options.media_path = media_path;
		options.geometry_directory = geometry_directory;
		options.output_path = output_path;
		if (!openvol::authoring::pack_openvol(options))
		{
			last_error =
				"Packaging or output verification failed. Check the encoder log.";
			return -1;
		}
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

const char* openvol_authoring_last_error()
{
	return last_error.c_str();
}
