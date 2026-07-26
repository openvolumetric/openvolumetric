#include "VolumetricVideoAuthoringApi.h"

#include "VolumetricVideoPacker.h"

#include <exception>
#include <string>

namespace
{
thread_local std::string last_error;
}

int volumetricvideo_authoring_pack(
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
		volumetric_video::authoring::PackOptions options;
		options.media_path = media_path;
		options.geometry_directory = geometry_directory;
		options.output_path = output_path;
		if (!volumetric_video::authoring::pack_volumetric_video(options))
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

const char* volumetricvideo_authoring_last_error()
{
	return last_error.c_str();
}
