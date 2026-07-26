#pragma once

#include <IAVDecoder.h>
#include <BoundedQueue.h>
#include <FFmpegMp4VolumetricContainer.h>
#include <GeometryPacket.h>

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswresample/swresample.h>
}

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
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
	volumetric_video::FrameMatchResult get_video_data(
		double presentation_time,
		double tolerance,
		double& actual_presentation_time,
		uint8_t** outputY,
		uint8_t** outputU,
		uint8_t** outputV) override;

	int read_audio(float* output, int sample_count) override;

	bool has_embedded_geometry() const override;

	bool get_geometry_data(
		double presentation_time,
		EncodedGeometryFrame& output) override;

	bool geometry_end_of_stream() const override;

	std::uint64_t playback_generation() const override;

	std::string get_last_error() const override;

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

	bool push_audio(const float* samples, size_t sample_count);

	void flush_audio();

	bool route_packet(volumetric_video::ContainerPacket packet);
	bool decode_packet(const volumetric_video::ContainerPacket& packet);
	bool drain_pending_packets();
	bool audio_can_accept_packet() const;
	bool perform_seek(double time);
	bool process_seek_request();


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
	void free_front_frame();


private:



	// --------------------------------------------------------------------------
	// Container owns AVFormatContext and all demux/seek operations.
	// --------------------------------------------------------------------------
	std::unique_ptr<volumetric_video::FFmpegMp4VolumetricContainer> m_container;

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
	volumetric_video::BoundedQueue<FrameData> m_video_frames;
	std::vector<float>		m_audio_samples;
	std::atomic<uint64_t>	m_audio_read_position;
	std::atomic<uint64_t>	m_audio_write_position;
	volumetric_video::BoundedQueue<EncodedGeometryFrame> m_geometry_frames;
	std::deque<volumetric_video::ContainerPacket> m_pending_video_packets;
	std::deque<volumetric_video::ContainerPacket> m_pending_audio_packets;
	std::deque<volumetric_video::ContainerPacket> m_pending_geometry_packets;
	std::optional<volumetric_video::ContainerPacket> m_deferred_packet;
	std::atomic<std::uint64_t> m_playback_generation;
	double m_last_video_packet_time = -1.0;
	double m_last_geometry_packet_time = -1.0;

	// Runtime seeks are submitted by an engine thread and executed only by the
	// demux thread, which exclusively owns FFmpeg container/codec mutation.
	mutable std::mutex m_seek_mutex;
	std::condition_variable m_seek_condition;
	std::optional<double> m_requested_seek;
	std::uint64_t m_seek_request_id = 0;
	std::uint64_t m_completed_seek_id = 0;
	bool m_seek_result = false;
	std::atomic<bool> m_thread_running{false};
	std::atomic<bool> m_stop_requested{false};

	// BoundedQueue protects video/geometry access and carries terminal state.
	// Audio uses atomic monotonic read/write positions.
	std::thread				m_decode_thread;

};
