#include "AuthoringWorkflow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace openvolumetric::authoring
{
namespace
{

struct NumberedFile
{
	int frame = 0;
	int digits = 0;
	std::string extension;
};

bool discover_sequence(
	const std::filesystem::path& directory,
	const std::set<std::string>& extensions,
	const char* label,
	std::vector<NumberedFile>& files,
	std::string& error)
{
	files.clear();
	if (!std::filesystem::is_directory(directory))
	{
		error = std::string(label) + " directory does not exist: " +
			directory.string();
		return false;
	}

	for (const auto& entry : std::filesystem::directory_iterator(directory))
	{
		if (!entry.is_regular_file())
			continue;
		std::string extension = entry.path().extension().string();
		std::transform(
			extension.begin(),
			extension.end(),
			extension.begin(),
			[](unsigned char value)
			{
				return static_cast<char>(std::tolower(value));
			});
		if (extensions.find(extension) == extensions.end())
			continue;

		const std::string stem = entry.path().stem().string();
		if (stem.empty() ||
			!std::all_of(stem.begin(), stem.end(), [](unsigned char value)
			{
				return std::isdigit(value) != 0;
			}))
		{
			continue;
		}
		try
		{
			files.push_back({
				std::stoi(stem),
				static_cast<int>(stem.size()),
				std::move(extension)});
		}
		catch (const std::exception&)
		{
			error = std::string(label) +
				" frame number is outside the supported integer range: " +
				entry.path().filename().string();
			return false;
		}
	}

	if (files.empty())
	{
		error = std::string("No numbered ") + label + " files were found.";
		return false;
	}
	std::sort(files.begin(), files.end(), [](const auto& left, const auto& right)
	{
		return left.frame < right.frame;
	});
	for (std::size_t index = 0; index < files.size(); ++index)
	{
		if (files[index].digits != files.front().digits ||
			files[index].extension != files.front().extension)
		{
			error = std::string(label) +
				" filenames must use consistent padding and extensions.";
			return false;
		}
		if (index > 0 && files[index].frame != files[index - 1].frame + 1)
		{
			error = std::string(label) + " sequence is not contiguous.";
			return false;
		}
	}
	return true;
}

std::string number(double value)
{
	std::ostringstream output;
	output << std::setprecision(12) << value;
	return output.str();
}

} // namespace

EncodingSettings preset_settings(PlatformPreset preset)
{
	switch (preset)
	{
	case PlatformPreset::DesktopLocal:
		return {
			VideoCodec::HEVC, 20, 60, 3, false,
			14, 10, 12, 5, 5, 0, 0, 0};
	case PlatformPreset::DesktopStreaming:
		return {
			VideoCodec::HEVC, 23, 60, 2, false,
			14, 10, 12, 5, 7, 16000, 32000, 60};
	case PlatformPreset::QuestLocal:
		return {
			VideoCodec::H264, 23, 30, 1, false,
			12, 8, 10, 8, 10, 0, 0, 0};
	case PlatformPreset::QuestStreaming:
	default:
		return {
			VideoCodec::HEVC, 27, 30, 1, true,
			12, 8, 10, 8, 10, 8000, 16000, 30};
	}
}

bool adaptive_ladder_settings(
	PlatformPreset preset,
	int fragment_duration_seconds,
	std::vector<AdaptiveLadderEntry>& entries,
	std::string& error)
{
	entries.clear();
	error.clear();
	if (preset != PlatformPreset::DesktopStreaming &&
		preset != PlatformPreset::QuestStreaming)
	{
		error = "Adaptive authoring requires a desktop or Quest streaming preset.";
		return false;
	}
	if (!is_supported_fragment_duration(fragment_duration_seconds) ||
		fragment_duration_seconds == 0)
	{
		error = "Adaptive authoring requires 1, 2, or 4 second fragments.";
		return false;
	}

	EncodingSettings high = preset_settings(preset);
	high.fragment_duration_seconds = fragment_duration_seconds;
	EncodingSettings low = high;
	low.crf = std::min(51, high.crf + 5);
	low.position_quantization = std::max(8, high.position_quantization - 2);
	low.normal_quantization = std::max(6, high.normal_quantization - 2);
	low.texture_quantization = std::max(8, high.texture_quantization - 2);
	low.maximum_video_bitrate_kbps =
		std::max(1, high.maximum_video_bitrate_kbps / 2);
	low.video_buffer_size_kbps =
		std::max(low.maximum_video_bitrate_kbps,
			high.video_buffer_size_kbps / 2);

	const std::string prefix = preset == PlatformPreset::QuestStreaming
		? "quest-streaming"
		: "desktop-streaming";
	entries.push_back({prefix + "-low", low});
	entries.push_back({prefix + "-high", high});
	return true;
}

bool is_supported_fragment_duration(int value)
{
	return value == 0 || value == 1 || value == 2 || value == 4;
}

int fragment_frame_interval(double frame_rate, int duration_seconds)
{
	if (duration_seconds == 0)
		return 0;
	if (!is_supported_fragment_duration(duration_seconds) ||
		!std::isfinite(frame_rate) || frame_rate <= 0.0)
	{
		return 0;
	}
	const double exact = frame_rate * duration_seconds;
	const long long rounded = std::llround(exact);
	if (rounded <= 0 ||
		std::abs(exact - static_cast<double>(rounded)) > 1e-6 ||
		rounded > std::numeric_limits<int>::max())
	{
		return 0;
	}
	return static_cast<int>(rounded);
}

bool validate_source_sequences(
	const std::filesystem::path& image_directory,
	const std::filesystem::path& geometry_directory,
	SourceSequenceInfo& output,
	std::string& error)
{
	error.clear();
	output = {};
	std::vector<NumberedFile> images;
	std::vector<NumberedFile> geometry;
	if (!discover_sequence(
			image_directory,
			{".jpg", ".jpeg", ".png", ".tif", ".tiff", ".exr"},
			"image",
			images,
			error) ||
		!discover_sequence(
			geometry_directory,
			{".obj"},
			"OBJ",
			geometry,
			error))
	{
		return false;
	}
	if (images.size() != geometry.size())
	{
		error = "Image and OBJ sequences have different frame counts.";
		return false;
	}
	for (std::size_t index = 0; index < images.size(); ++index)
	{
		if (images[index].frame != geometry[index].frame)
		{
			error = "Image and OBJ frame numbers do not match.";
			return false;
		}
	}

	output.first_frame = images.front().frame;
	output.frame_count = images.size();
	output.filename_digits = images.front().digits;
	output.image_extension = images.front().extension;
	return true;
}

bool build_ffmpeg_arguments(
	const MediaEncodeRequest& request,
	std::vector<std::string>& arguments,
	std::string& error)
{
	error.clear();
	arguments.clear();
	if (request.image_pattern.empty() ||
		request.output_path.empty() ||
		request.frame_count == 0 ||
		!std::isfinite(request.frame_rate) ||
		request.frame_rate <= 0.0)
	{
		error = "A valid image pattern, output, frame rate, and frame count are required.";
		return false;
	}
	const EncodingSettings& settings = request.settings;
	const int fragment_frames = fragment_frame_interval(
		request.frame_rate, settings.fragment_duration_seconds);
	if (settings.crf < 0 ||
		settings.video_keyframe_interval <= 0 ||
		settings.reference_frames <= 0 ||
		settings.maximum_video_bitrate_kbps < 0 ||
		settings.video_buffer_size_kbps < 0 ||
		((settings.maximum_video_bitrate_kbps == 0) !=
			(settings.video_buffer_size_kbps == 0)) ||
		!is_supported_fragment_duration(settings.fragment_duration_seconds) ||
		(settings.fragment_duration_seconds > 0 && fragment_frames == 0))
	{
		error = "Codec, bitrate, or keyframe settings are invalid.";
		return false;
	}

	arguments = {
		"-hide_banner",
		"-y",
		"-framerate", number(request.frame_rate),
		"-start_number", std::to_string(request.first_frame),
		"-i", request.image_pattern.string()};
	if (!request.audio_path.empty())
	{
		arguments.push_back("-i");
		arguments.push_back(request.audio_path.string());
	}
	arguments.insert(arguments.end(), {
		"-frames:v", std::to_string(request.frame_count),
		"-c:v", settings.codec == VideoCodec::HEVC ? "libx265" : "libx264",
		"-crf", std::to_string(settings.crf),
		"-pix_fmt", "yuv420p"});
	if (settings.maximum_video_bitrate_kbps > 0)
	{
		arguments.insert(arguments.end(), {
			"-maxrate",
			std::to_string(settings.maximum_video_bitrate_kbps) + "k",
			"-bufsize",
			std::to_string(settings.video_buffer_size_kbps) + "k"});
	}
	const int keyframe_interval = fragment_frames > 0
		? fragment_frames
		: settings.video_keyframe_interval;
	const std::string codec_parameters =
		"keyint=" + std::to_string(keyframe_interval) +
		":min-keyint=" + std::to_string(
			fragment_frames > 0 ? keyframe_interval : 1) +
		(fragment_frames > 0 ? ":scenecut=0" : "") +
		":bframes=0:ref=" +
		std::to_string(settings.reference_frames) +
		(settings.codec == VideoCodec::HEVC && settings.disable_sao
			? ":no-sao=1"
			: "");
	if (settings.codec == VideoCodec::HEVC)
	{
		arguments.insert(
			arguments.end(), {"-x265-params", codec_parameters});
	}
	else
	{
		arguments.insert(
			arguments.end(),
			{"-preset", "fast", "-x264-params", codec_parameters});
	}
	if (request.audio_path.empty())
	{
		arguments.push_back("-an");
	}
	else
	{
		arguments.insert(
			arguments.end(),
			{"-c:a", "aac", "-b:a", "192k",
			 "-af", "apad", "-shortest"});
	}
	arguments.push_back(request.output_path.string());
	return true;
}

} // namespace openvolumetric::authoring
