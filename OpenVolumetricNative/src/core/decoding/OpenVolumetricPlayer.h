#pragma once

#include <IAVDecoder.h>
#include <IGeometryDecoder.h>
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
	/// Decoded luma width in pixels.
	int width = 0;
	/// Decoded luma height in pixels.
	int height = 0;
	/// Nominal video rate used for presentation tolerances.
	double frame_rate = 0.0;
	/// Presentation duration in seconds.
	double duration = 0.0;
	/// Whether a supported audio track was opened.
	bool has_audio = false;
	/// Decoded PCM rate, or zero when audio is absent.
	int audio_sample_rate = 0;
	/// Interleaved decoded channel count, or zero when audio is absent.
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
	std::uint64_t transfer_throughput_bits_per_second = 0;
	std::uint64_t request_count = 0;
	std::uint64_t recovery_count = 0;
	bool fragmented = false;
	std::uint64_t fragment_count = 0;
	std::int64_t active_fragment = -1;
	std::uint64_t cached_fragment_count = 0;
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
	/// Constructs a closed player with no workers or codec resources.
	OpenVolumetricPlayer();
	/// Internal/test seam taking complete ownership of two non-null decoders.
	///
	/// Production integrations use the default constructor. This overload keeps
	/// concrete FFmpeg, container, byte-source, and Draco types behind the
	/// façade while allowing core tests to supply narrow interface substitutes.
	OpenVolumetricPlayer(
		std::unique_ptr<IAVDecoder> media_decoder,
		std::unique_ptr<IGeometryDecoder> geometry_decoder);
	/// Idempotently stops workers and releases all owned decoder state.
	~OpenVolumetricPlayer();

	OpenVolumetricPlayer(const OpenVolumetricPlayer&) = delete;
	OpenVolumetricPlayer& operator=(const OpenVolumetricPlayer&) = delete;

	/// Opens one local path or HTTP(S) OpenVolumetric representation.
	bool open(const char* path);
	/// Starts media and geometry decoder workers after a successful open.
	bool start();
	/// Stops and joins decoder workers while retaining opened metadata.
	void stop();
	/// Cancels blocking local or HTTP input without waiting for worker shutdown.
	void cancel_pending_io();
	/// Idempotently stops and releases all media, queue, and decoder resources.
	void close();
	/// Resets all modalities and prepares a presentation at the requested time.
	bool seek(double time);

	/// Returns immutable metadata valid until close() or the next open().
	const OpenVolumetricMediaInfo& media_info() const;
	/// Returns a thread-safe input transport/cache snapshot.
	OpenVolumetricBufferInfo buffer_info() const;
	/// Returns a thread-safe decoded PCM snapshot.
	OpenVolumetricAudioBufferInfo audio_buffer_info() const;
	/// Returns the latest persistent failure message.
	const std::string& error() const;

	/// Copies one timestamp-matched texture/geometry presentation into output.
	FrameMatchResult presentation(
		double requested_time,
		OpenVolumetricPresentation& output);
	/// Fills interleaved float PCM, writing silence for unavailable samples.
	int read_audio(float* output, int sample_count);

private:
	bool submit_geometry(double requested_time);

	class Impl;
	std::unique_ptr<Impl> m_impl;
};

} // namespace openvolumetric
