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

namespace openvol
{

/// FFmpeg implementation of the combined volumetric MP4 decoder.
///
/// A single worker owns av_read_frame() and routes packets by stream:
/// video is decoded to queued YUV frames, audio to a lock-free PCM ring, and
/// vvge samples to a mutex-protected compressed-geometry queue.
class AVDecoderFFMPEG : public IAVDecoder
{

public:
	/// Constructs empty queues, codec pointers, and seek coordination state.
	AVDecoderFFMPEG();
	
	/// Stops decoding and releases all FFmpeg resources.
	~AVDecoderFFMPEG() override;

	/// Opens the combined MP4 and initializes all discovered stream decoders.
	bool init(const char* filename) override;

	/// Starts the sole demux/decode worker.
	bool start_decoding() override;

	/// Requests termination, wakes pending seeks, and joins the worker.
	bool stop_decoding() override;

	/// Worker loop that routes packets and services serialized seek requests.
	bool decode() override;

	/// Submits a synchronous seek request to the demux-owner thread.
	bool seek(double time) override;

	/// Selects the closest decoded YUV frame and returns borrowed plane data.
	openvol::FrameMatchResult get_video_data(
		double presentation_time,
		double tolerance,
		double& actual_presentation_time,
		uint8_t** outputY,
		uint8_t** outputU,
		uint8_t** outputV) override;

	/// Copies interleaved PCM from the single-producer/single-consumer ring.
	int read_audio(float* output, int sample_count) override;

	/// Returns whether init_geometry_context() found a valid vvge stream.
	bool has_embedded_geometry() const override;

	/// Removes the oldest geometry sample not newer than presentation_time.
	bool get_geometry_data(
		double presentation_time,
		EncodedGeometryFrame& output) override;

	/// Returns whether geometry production ended for the current generation.
	bool geometry_end_of_stream() const override;

	/// Identifies the seek/loop pass owning currently queued samples.
	std::uint64_t playback_generation() const override;

	/// Combines persistent container and queue errors for engine diagnostics.
	std::string get_last_error() const override;

	/// Stops the worker and frees container, codecs, frames, and audio state.
	void destroy() override;

protected:
	/// Opens the container and initializes the packet used for codec draining.
	bool init_ffmpeg_context(const char* filepath);

	/// Creates and configures the video codec context.
	bool init_video_context();

	/// Creates the audio codec and float-interleaved resampler.
	bool init_audio_context();

	/// Records the required geometry stream and its timing information.
	bool init_geometry_context();

	/// Receives all currently available video frames from the active packet.
	bool decode_video_frame();

	/// Receives, resamples, and queues audio from the active packet.
	bool decode_audio_frame();

	/// Parses the active vvge packet and queues its compressed Draco payload.
	bool queue_geometry_packet();

	/// Appends PCM to the bounded SPSC ring without overwriting unread data.
	bool push_audio(const float* samples, size_t sample_count);

	/// Resets audio ring positions and zeroes its storage.
	void flush_audio();

	/// Adds packet to its stream queue while applying bounded backpressure.
	bool route_packet(openvol::ContainerPacket packet);
	/// Converts one owning container packet into an AVPacket and decodes it.
	bool decode_packet(const openvol::ContainerPacket& packet);
	/// Gives each stream queue a fair opportunity to make progress.
	bool drain_pending_packets();
	/// Returns whether the audio ring has enough headroom for another packet.
	bool audio_can_accept_packet() const;
	/// Runs all container/codec/queue mutations required by a seek.
	bool perform_seek(double time);
	/// Services one engine-submitted seek on the demux owner thread.
	bool process_seek_request();

	/// Releases the video frame currently retained for presentation.
	void clean_frame_data() override;

	/// Flushes codec state and every compressed/decoded output queue.
	void flush_buffers();

private:

	/// Owned decoded video frame and its presentation timestamp.
	struct FrameData
	{
		/// Constructs an empty frame holder.
		FrameData() : data(NULL), frame_time(0.0){};

		// Owned FFmpeg frame; released when removed from m_video_frames.
		AVFrame* data;

		// Presentation time in seconds.
		double frame_time;
	};


	/// Frees and removes the oldest queued video frame atomically.
	void free_front_frame();


private:



	// --------------------------------------------------------------------------
	// Container owns AVFormatContext and all demux/seek operations.
	// --------------------------------------------------------------------------
	std::unique_ptr<openvol::FFmpegMp4VolumetricContainer> m_container;

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
	openvol::BoundedQueue<FrameData> m_video_frames;
	std::vector<float>		m_audio_samples;
	std::atomic<uint64_t>	m_audio_read_position;
	std::atomic<uint64_t>	m_audio_write_position;
	double					m_audio_discard_before = -1.0;
	openvol::BoundedQueue<EncodedGeometryFrame> m_geometry_frames;
	std::deque<openvol::ContainerPacket> m_pending_video_packets;
	std::deque<openvol::ContainerPacket> m_pending_audio_packets;
	std::deque<openvol::ContainerPacket> m_pending_geometry_packets;
	std::optional<openvol::ContainerPacket> m_deferred_packet;
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

} // namespace openvol
