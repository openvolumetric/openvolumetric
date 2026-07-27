#pragma once

#include <IDecoder.h>
#include <Stream.h>
#include <TimedFrame.h>

#include <cstdint>
#include <string>
#include <vector>

namespace openvol
{

/// Engine-independent view of the combined MP4 decoder.
///
/// Implementations own demuxing and media-codec state. Consumers retrieve
/// decoded video planes, PCM audio, and timestamped compressed geometry
/// without depending on FFmpeg types.
class IAVDecoder : public IDecoder
{

public:

	/// Video dimensions, nominal rate, and container timing.
	struct VideoInfo : public StreamInfo
	{
		/// Constructs metadata for an unavailable video stream.
		VideoInfo() : StreamInfo(), width(0), height(0), fps(0.0), frame_count(0) {}

		int width;
		int height;
		double fps;
		int frame_count;
	};
	
	/// Decoded PCM layout and container timing.
	struct AudioInfo : public StreamInfo
	{
		/// Constructs metadata for an unavailable audio stream.
		AudioInfo() : StreamInfo(), sample_rate(0), channels(0) {}

		int sample_rate;
		int channels;
	};

	using EncodedGeometryFrame = openvol::CompressedGeometryFrame;

	/// Constructs an uninitialized combined-media decoder.
	IAVDecoder():IDecoder(){};
	
	/// Releases an implementation through the media-decoder interface.
	~IAVDecoder() override {};
	
	/// Returns a snapshot of video metadata established by init().
	VideoInfo get_video_info() const { return m_video_info; };

	/// Returns a snapshot of audio metadata established by init().
	AudioInfo get_audio_info() const { return m_audio_info; };

	/// Opens the combined MP4 and discovers its video, audio, and vvge tracks.
	virtual bool init(const char* filepath) = 0;

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	/// Worker entry point that demuxes and decodes until stopped or at EOS.
	virtual bool decode() = 0;

	/// Seeks all streams to a presentation time measured in seconds.
	virtual bool seek(double time) = 0;

	/// Selects the decoded video sample nearest presentation_time.
	virtual openvol::FrameMatchResult get_video_data(
		double presentation_time,
		double tolerance,
		double& actual_presentation_time,
		uint8_t** outputY,
		uint8_t** outputU,
		uint8_t** outputV) = 0;

	/// Copies interleaved floating-point PCM into output. Missing samples are
	/// represented as silence by the implementation.
	virtual int read_audio(float* output, int sample_count) = 0;

	/// True when the required project-specific geometry track was discovered.
	virtual bool has_embedded_geometry() const = 0;

	/// Removes the oldest queued geometry sample up to presentation_time.
	virtual bool get_geometry_data(
		double presentation_time,
		EncodedGeometryFrame& output) = 0;

	/// Returns whether the geometry producer reached EOS for this generation.
	virtual bool geometry_end_of_stream() const = 0;

	/// Increments whenever playback is reset or loops to a new timeline pass.
	/// Returns the current seek/loop generation identifier.
	virtual std::uint64_t playback_generation() const = 0;

	/// Returns a persistent decoder or queue error suitable for display.
	virtual std::string get_last_error() const = 0;

	/// Releases the currently selected video frame.
	virtual void clean_frame_data() = 0;

	/// Stops workers and releases all codec, container, and queue resources.
	virtual void destroy() = 0;


protected:

	/// Metadata populated by concrete decoder initialization.
	VideoInfo m_video_info;
	AudioInfo m_audio_info;

	
};

} // namespace openvol
