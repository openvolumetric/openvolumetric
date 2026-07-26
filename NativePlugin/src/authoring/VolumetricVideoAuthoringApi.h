#pragma once

#if defined(_WIN32)
	#if defined(VOLUMETRIC_VIDEO_AUTHORING_EXPORTS)
		#define VV_AUTHORING_API __declspec(dllexport)
	#else
		#define VV_AUTHORING_API __declspec(dllimport)
	#endif
#else
	#define VV_AUTHORING_API __attribute__((visibility("default")))
#endif

extern "C"
{

/// Encodes one OBJ mesh using the Draco library linked into this authoring
/// module. Returns 1 on success and -1 on failure.
VV_AUTHORING_API int volumetricvideo_authoring_encode_obj(
	const char* input_path,
	const char* output_path,
	int position_quantization,
	int normal_quantization,
	int texture_quantization);

/// Packages an existing video/audio MP4 and numbered Draco directory.
/// Returns 1 on success and -1 on failure.
VV_AUTHORING_API int volumetricvideo_authoring_pack(
	const char* media_path,
	const char* geometry_directory,
	const char* output_path);

/// Returns a short description of the most recent failure on this thread.
VV_AUTHORING_API const char* volumetricvideo_authoring_last_error();

}
