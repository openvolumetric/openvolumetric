#pragma once

#include <IAVDecoder.h>
#include <GeometryPacket.h>

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswresample/swresample.h>
}

#include <atomic>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

/// FFmpeg implementation of the combined volumetric MP4 decoder.
///
/// A single worker owns av_read_frame() and routes packets by stream:
/// video is decoded to queued YUV frames, audio to a lock-free PCM ring, and
/// vvge samples to a mutex-protected compressed-geometry queue.
class AVDecoderFFMPEG : public IAVDecoder
{

public:
	
	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	AVDecoderFFMPEG();
	

	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	~AVDecoderFFMPEG() override;


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool init(const char* filename) override;


	// --------------------------------------------------------------------------
	// Start Decoding
	//
	bool start_decoding() override;


	// --------------------------------------------------------------------------
	// Stop Decoding
	// --------------------------------------------------------------------------
	bool stop_decoding() override;


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool decode() override;


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool seek(double time) override;


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool get_video_data(int frame_index, uint8_t** outputY, uint8_t** outputU, uint8_t** outputV) override;

	int read_audio(float* output, int sample_count) override;

	bool has_embedded_geometry() const override;

	bool get_geometry_data(
		int frame_index,
		EncodedGeometryFrame& output) override;

	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	void destroy() override;

protected:

	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool init_ffmpeg_context(const char* filepath);


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool init_video_context();

	bool init_audio_context();

	bool init_geometry_context();


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool is_buffer_blocked();


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	void update_buffer_state();


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	bool decode_video_frame();

	bool decode_audio_frame();

	bool queue_geometry_packet();

	void push_audio(const float* samples, size_t sample_count);

	void flush_audio();


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	void clean_frame_data() override;


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	void flush_buffers();

private:

	// --------------------------------------------------------------------------
	// Data structure to encapsulate frame data
	// --------------------------------------------------------------------------
	struct FrameData
	{
		FrameData() : data(NULL), frame_index(0), frame_time(0.0){};

		// Owned FFmpeg frame; released when removed from m_video_frames.
		AVFrame* data;

		// Presentation index derived from best_effort_timestamp.
		int frame_index;

		// Presentation time in seconds.
		double frame_time;
	};


	// --------------------------------------------------------------------------
	//
	// --------------------------------------------------------------------------
	void free_front_frame(std::queue<FrameData>* buffer, std::mutex* mutex);


private:



	// --------------------------------------------------------------------------
	// AVFormat Context
	// --------------------------------------------------------------------------
	AVFormatContext*		m_avformat_ctx;

	// --------------------------------------------------------------------------
	// Video Information
	// --------------------------------------------------------------------------
	int						m_video_stream_index;
	AVStream*				m_video_stream;
	AVCodecContext*			m_video_codec_ctx;
	const AVCodec*			m_video_codec;

	int						m_audio_stream_index;
	AVStream*				m_audio_stream;
	AVCodecContext*			m_audio_codec_ctx;
	const AVCodec*			m_audio_codec;
	SwrContext*				m_audio_resampler;

	int						m_geometry_stream_index;
	AVStream*				m_geometry_stream;

	// Queues bridge the decoder worker and engine/audio consumer threads.
	AVPacket				m_packet;
	std::queue<FrameData>	m_video_frames;
	unsigned int			m_video_buffer_max;
	std::vector<float>		m_audio_samples;
	std::atomic<uint64_t>	m_audio_read_position;
	std::atomic<uint64_t>	m_audio_write_position;
	std::queue<EncodedGeometryFrame> m_geometry_frames;

	// m_video_mutex and m_geometry_mutex protect their corresponding queues.
	// Audio uses atomic monotonic read/write positions instead of a mutex.
	std::thread				m_decode_thread;
	std::mutex				m_video_mutex;
	std::mutex				m_geometry_mutex;

};
