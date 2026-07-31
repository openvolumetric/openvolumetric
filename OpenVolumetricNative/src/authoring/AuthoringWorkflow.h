#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace openvolumetric::authoring
{

enum class VideoCodec
{
	H264,
	HEVC
};

enum class PlatformPreset
{
	DesktopLocal,
	DesktopStreaming,
	QuestLocal,
	QuestStreaming
};

/// Codec settings shared by the Unity and Unreal authoring interfaces.
struct EncodingSettings
{
	VideoCodec codec = VideoCodec::HEVC;
	int crf = 20;
	int video_keyframe_interval = 60;
	int reference_frames = 3;
	bool disable_sao = false;
	int position_quantization = 14;
	int normal_quantization = 10;
	int texture_quantization = 12;
	int draco_encode_speed = 5;
	int draco_decode_speed = 5;
	int maximum_video_bitrate_kbps = 0;
	int video_buffer_size_kbps = 0;
	int geometry_keyframe_interval = 0;
	/// Zero writes a conventional fast-start MP4. Supported non-zero values
	/// write aligned fragmented MP4 with this fragment duration in seconds.
	int fragment_duration_seconds = 0;
};

/// Returns whether value selects one of the supported fragment durations.
bool is_supported_fragment_duration(int value);

/// Converts an enabled fragment duration to an integral frame interval.
/// Returns zero for conventional MP4 or when rate/duration is invalid.
int fragment_frame_interval(double frame_rate, int duration_seconds);

/// Result of validating matching numbered image and OBJ sequences.
struct SourceSequenceInfo
{
	int first_frame = 0;
	std::size_t frame_count = 0;
	int filename_digits = 0;
	std::string image_extension;
};

/// Inputs used to construct one FFmpeg image/audio encoding invocation.
struct MediaEncodeRequest
{
	std::filesystem::path image_pattern;
	std::filesystem::path audio_path;
	std::filesystem::path output_path;
	double frame_rate = 30.0;
	int first_frame = 0;
	std::size_t frame_count = 0;
	EncodingSettings settings;
};

EncodingSettings preset_settings(PlatformPreset preset);

/// Validates contiguous, equally numbered image and OBJ sequences.
bool validate_source_sequences(
	const std::filesystem::path& image_directory,
	const std::filesystem::path& geometry_directory,
	SourceSequenceInfo& output,
	std::string& error);

/// Returns individual arguments, excluding the FFmpeg executable itself.
bool build_ffmpeg_arguments(
	const MediaEncodeRequest& request,
	std::vector<std::string>& arguments,
	std::string& error);

} // namespace openvolumetric::authoring
