#pragma once

#if defined(_WIN32)
	#if defined(OPENVOLUMETRIC_AUTHORING_EXPORTS)
		#define OPENVOLUMETRIC_AUTHORING_API __declspec(dllexport)
	#else
		#define OPENVOLUMETRIC_AUTHORING_API __declspec(dllimport)
	#endif
#else
	#define OPENVOLUMETRIC_AUTHORING_API __attribute__((visibility("default")))
#endif

extern "C"
{

/// ABI-stable representation of the shared platform-preset settings.
struct OpenVolumetricAuthoringSettings
{
	int codec;
	int crf;
	int video_keyframe_interval;
	int reference_frames;
	int disable_sao;
	int position_quantization;
	int normal_quantization;
	int texture_quantization;
	int draco_encode_speed;
	int draco_decode_speed;
};

/// Writes one shared preset into output. Presets are Desktop (0), Quest
/// Balanced (1), and Quest Performance (2).
OPENVOLUMETRIC_AUTHORING_API int openvolumetric_authoring_get_preset(
	int preset,
	OpenVolumetricAuthoringSettings* output);

/// Validates matching, contiguous image and OBJ sequences.
OPENVOLUMETRIC_AUTHORING_API int openvolumetric_authoring_validate_sources(
	const char* image_directory,
	const char* geometry_directory);

/// Constructs the shared FFmpeg argument list. Arguments are separated by
/// newlines so managed callers can pass each item through their process API.
OPENVOLUMETRIC_AUTHORING_API const char*
	openvolumetric_authoring_build_ffmpeg_arguments(
		const char* image_pattern,
		const char* audio_path,
		const char* output_path,
		double frame_rate,
		int first_frame,
		int frame_count,
		const OpenVolumetricAuthoringSettings* settings);

/// Encodes one OBJ mesh using the Draco library linked into this authoring
/// module. Returns 1 on success and -1 on failure.
OPENVOLUMETRIC_AUTHORING_API int openvolumetric_authoring_encode_obj(
	const char* input_path,
	const char* output_path,
	int position_quantization,
	int normal_quantization,
	int texture_quantization,
	int encode_speed,
	int decode_speed,
	int enable_topology_compression);

/// Packages an existing video/audio MP4 and numbered Draco directory. The
/// matching OBJ directory enables automatic topology-aware packets.
/// Returns 1 on success and -1 on failure.
OPENVOLUMETRIC_AUTHORING_API int openvolumetric_authoring_pack(
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
	int maximum_geometry_keyframe_interval);

/// Returns a short description of the most recent failure on this thread.
OPENVOLUMETRIC_AUTHORING_API const char* openvolumetric_authoring_last_error();

/// Returns the statistics summary from the most recent successful pack.
OPENVOLUMETRIC_AUTHORING_API const char* openvolumetric_authoring_last_report();

}
