#pragma once

#include <IDecoder.h>
#include <Stream.h>

#include <cstdint>
#include <vector>

/// Engine-independent view of the combined MP4 decoder.
///
/// Implementations own demuxing and media-codec state. Consumers retrieve
/// decoded video planes, PCM audio, and timestamped compressed geometry
/// without depending on FFmpeg types.
class IAVDecoder : public IDecoder
{

public:

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	struct VideoInfo : public StreamInfo
	{
		VideoInfo() : StreamInfo(), width(0), height(0), fps(0.0), frame_count(0) {}

		int width;
		int height;
		double fps;
		int frame_count;
	};
	
	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	struct AudioInfo : public StreamInfo
	{
		AudioInfo() : StreamInfo(), sample_rate(0), channels(0) {}

		int sample_rate;
		int channels;
	};

	struct EncodedGeometryFrame
	{
		// Presentation index derived from the geometry sample PTS.
		int frame_index = -1;
		// Presentation timestamp in seconds.
		double presentation_time = 0.0;
		// Original authoring frame number stored in the VVGF header. This is
		// diagnostic metadata; synchronization uses presentation_time.
		std::uint32_t source_frame_number = 0;
		// Complete Draco bitstream, with the VVGF header removed.
		std::vector<std::uint8_t> payload;
	};

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	IAVDecoder():IDecoder(){};
	
	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	~IAVDecoder() override {};
	
	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	VideoInfo get_video_info() { return m_video_info; };

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	AudioInfo get_audio_info() { return m_audio_info; };

	/// Opens the combined MP4 and discovers its video, audio, and vvge tracks.
	virtual bool init(const char* filepath) = 0;

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	virtual bool decode() = 0;

	/// Seeks all streams to a presentation time measured in seconds.
	virtual bool seek(double time) = 0;

	/// Returns planar YUV420 data for exactly frame_index when available.
	/// Pointers remain owned by the decoder.
	virtual bool get_video_data(int frame_index, uint8_t** outputY, uint8_t** outputU, uint8_t** outputV) = 0;

	/// Copies interleaved floating-point PCM into output. Missing samples are
	/// represented as silence by the implementation.
	virtual int read_audio(float* output, int sample_count) = 0;

	/// True when the required project-specific geometry track was discovered.
	virtual bool has_embedded_geometry() const = 0;

	/// Removes and returns the oldest queued geometry frame whose presentation
	/// index is no later than frame_index.
	virtual bool get_geometry_data(
		int frame_index,
		EncodedGeometryFrame& output) = 0;

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	virtual void clean_frame_data() = 0;

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	virtual void destroy() = 0;


protected:

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	VideoInfo m_video_info;
	AudioInfo m_audio_info;

	
};
