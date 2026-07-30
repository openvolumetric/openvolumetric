#include "AuthoringWorkflow.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <iomanip>
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
	if (settings.crf < 0 ||
		settings.video_keyframe_interval <= 0 ||
		settings.reference_frames <= 0 ||
		settings.maximum_video_bitrate_kbps < 0 ||
		settings.video_buffer_size_kbps < 0 ||
		((settings.maximum_video_bitrate_kbps == 0) !=
			(settings.video_buffer_size_kbps == 0)))
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
	const std::string codec_parameters =
		"keyint=" + std::to_string(settings.video_keyframe_interval) +
		":min-keyint=1:bframes=0:ref=" +
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
