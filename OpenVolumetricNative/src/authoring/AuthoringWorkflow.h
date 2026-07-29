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
	DesktopQuality,
	QuestBalanced,
	QuestPerformance
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
};

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
