#pragma once

#include <IAVDecoder.h>
#include <Mesh.h>
#include <TimedFrame.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openvolumetric
{

/// Stable stream metadata returned without exposing FFmpeg structures.
struct OpenVolumetricMediaInfo
{
	int width = 0;
	int height = 0;
	double frame_rate = 0.0;
	double duration = 0.0;
	bool has_audio = false;
	int audio_sample_rate = 0;
	int audio_channels = 0;
};

/// Engine-facing snapshot of input transport and bounded-cache activity.
struct OpenVolumetricBufferInfo
{
	ByteSourceState state = ByteSourceState::Opening;
	bool remote = false;
	std::int64_t resource_size_bytes = -1;
	std::uint64_t cached_bytes = 0;
	std::uint64_t downloaded_bytes = 0;
	std::uint64_t request_count = 0;
	std::uint64_t recovery_count = 0;
};

/// Engine-facing snapshot of decoded PCM readiness and consumption.
struct OpenVolumetricAudioBufferInfo
{
	double read_time = 0.0;
	double buffered_duration = 0.0;
	std::uint64_t underrun_count = 0;
};

/// One owned, timestamp-matched visual presentation.
struct OpenVolumetricPresentation
{
	double presentation_time = 0.0;
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> y;
	std::vector<std::uint8_t> u;
	std::vector<std::uint8_t> v;
	Mesh mesh;
};

/// Engine-neutral façade for OpenVolumetric playback.
///
/// The façade owns FFmpeg and Draco workers and returns owned CPU data. Host
/// integrations never see codec types or retain decoder-owned frame pointers.
class OpenVolumetricPlayer final
{
public:
	OpenVolumetricPlayer();
	~OpenVolumetricPlayer();

	OpenVolumetricPlayer(const OpenVolumetricPlayer&) = delete;
	OpenVolumetricPlayer& operator=(const OpenVolumetricPlayer&) = delete;

	bool open(const char* path);
	bool start();
	void stop();
	void close();
	bool seek(double time);

	const OpenVolumetricMediaInfo& media_info() const;
	OpenVolumetricBufferInfo buffer_info() const;
	OpenVolumetricAudioBufferInfo audio_buffer_info() const;
	const std::string& error() const;

	FrameMatchResult presentation(
		double requested_time,
		OpenVolumetricPresentation& output);
	int read_audio(float* output, int sample_count);

private:
	bool submit_geometry(double requested_time);

	class Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace openvolumetric
