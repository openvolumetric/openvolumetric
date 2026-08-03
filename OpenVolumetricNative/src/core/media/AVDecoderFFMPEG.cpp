#include "AVDecoderFFMPEG.h"

#include <Logger.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace openvolumetric
{
AVDecoderFFMPEG::AVDecoderFFMPEG()
	: IAVDecoder(),
	  m_container(new openvolumetric::FFmpegMp4VolumetricContainer()),
	  m_video_frames(32),
	  m_geometry_frames(128),
	  m_playback_generation(0)
{
	// Video
	m_video_stream_index	= -1;
	m_video_stream			= nullptr;
	m_video_codec_ctx		= nullptr;
	m_video_codec			= nullptr;

	// Audio
	m_audio_stream_index	= -1;
	m_audio_stream			= nullptr;
	m_audio_codec_ctx		= nullptr;
	m_audio_codec			= nullptr;
	m_audio_resampler		= nullptr;
	m_audio_read_position.store(0);
	m_audio_write_position.store(0);
	m_audio_timeline_origin_position.store(0);
	m_audio_timeline_origin_time.store(0.0);
	m_audio_underrun_count.store(0);

	// Embedded geometry
	m_geometry_stream_index = -1;
	m_geometry_stream = nullptr;

	// Initialise the packet without the removed av_init_packet API.
	m_packet = {};
}
AVDecoderFFMPEG::~AVDecoderFFMPEG()
{
	LOG("AVDecoderFFMPEG::~AVDecoderFFMPEG" );
}
void AVDecoderFFMPEG::destroy()
{
	LOG("AVDecoderFFMPEG::destroy - start");

	// Join the demux worker before releasing any codec, queue, or packet state.
	stop_decoding();
	flush_buffers();

	m_container->close();

	//Video Variables
	if (m_video_codec_ctx != nullptr)
	{
		avcodec_free_context(&m_video_codec_ctx);
	}
	m_video_codec = nullptr;
	m_video_stream = nullptr;

	if (m_audio_resampler != nullptr)
	{
		swr_free(&m_audio_resampler);
	}
	if (m_audio_codec_ctx != nullptr)
	{
		avcodec_free_context(&m_audio_codec_ctx);
	}
	m_audio_codec = nullptr;
	m_audio_stream = nullptr;
	flush_audio();
	m_audio_samples.clear();
	m_geometry_frames.clear();
	m_pending_video_packets.clear();
	m_pending_audio_packets.clear();
	m_pending_geometry_packets.clear();
	m_deferred_packet.reset();
	m_geometry_stream = nullptr;
	m_geometry_stream_index = -1;

	av_packet_unref(&m_packet);

	// Other Variables
	m_initialised = false;
	m_decoder_state = UNINITIALIZED;

	LOG("AVDecoderFFMPEG::destroy - stop");
}
// Initialise the optional audio stream and convert it to interleaved stereo
// float samples suitable for Unity's streaming AudioClip callback.
bool AVDecoderFFMPEG::init_audio_context()
{
	int stream = m_container->stream_index(
		openvolumetric::StreamKind::Audio);
	if (stream < 0)
	{
		LOG("AVDecoderFFMPEG::init_audio_context - no audio stream");
		return true;
	}

	m_audio_stream_index = stream;
	m_audio_stream = m_container->native_stream(
		openvolumetric::StreamKind::Audio);
	m_audio_codec = avcodec_find_decoder(m_audio_stream->codecpar->codec_id);
	if (m_audio_codec == nullptr)
	{
		LOG("AVDecoderFFMPEG::init_audio_context - audio codec not available");
		return false;
	}

	m_audio_codec_ctx = avcodec_alloc_context3(m_audio_codec);
	if (m_audio_codec_ctx == nullptr ||
		avcodec_parameters_to_context(
			m_audio_codec_ctx, m_audio_stream->codecpar) < 0 ||
		avcodec_open2(m_audio_codec_ctx, m_audio_codec, nullptr) < 0)
	{
		LOG("AVDecoderFFMPEG::init_audio_context - could not open audio codec");
		return false;
	}

	AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	const int sample_rate = m_audio_codec_ctx->sample_rate;
	int ret = swr_alloc_set_opts2(
		&m_audio_resampler,
		&stereo,
		AV_SAMPLE_FMT_FLT,
		sample_rate,
		&m_audio_codec_ctx->ch_layout,
		m_audio_codec_ctx->sample_fmt,
		sample_rate,
		0,
		nullptr);
	if (ret < 0 || m_audio_resampler == nullptr ||
		swr_init(m_audio_resampler) < 0)
	{
		LOG("AVDecoderFFMPEG::init_audio_context - resampler init failed");
		return false;
	}

	m_audio_info.is_enabled = true;
	m_audio_info.sample_rate = sample_rate;
	m_audio_info.channels = stereo.nb_channels;
	m_audio_info.total_time = std::max(
		m_container->duration_seconds(),
		m_audio_stream->duration <= 0
			? 0.0
			: m_audio_stream->duration * av_q2d(m_audio_stream->time_base));

	// Four seconds absorbs decoder and render-thread scheduling jitter.
	m_audio_samples.resize(
		static_cast<size_t>(sample_rate) *
		static_cast<size_t>(m_audio_info.channels) * 4u);
	m_audio_read_position.store(0, std::memory_order_relaxed);
	m_audio_write_position.store(0, std::memory_order_relaxed);
	m_audio_timeline_origin_position.store(0, std::memory_order_relaxed);
	m_audio_timeline_origin_time.store(0.0, std::memory_order_relaxed);
	m_audio_underrun_count.store(0, std::memory_order_relaxed);

	LOG("AVDecoderFFMPEG::init_audio_context - Audio Stream: %d", stream);
	LOG("AVDecoderFFMPEG::init_audio_context - Codec: %s",
		m_audio_codec->name);
	LOG("AVDecoderFFMPEG::init_audio_context - Sample Rate: %d", sample_rate);
	LOG("AVDecoderFFMPEG::init_audio_context - Channels: %d",
		m_audio_info.channels);
	return true;
}

bool AVDecoderFFMPEG::init_geometry_context()
{
	m_geometry_stream_index = m_container->stream_index(
		openvolumetric::StreamKind::Geometry);
	m_geometry_stream = m_container->native_stream(
		openvolumetric::StreamKind::Geometry);
	if (m_geometry_stream == nullptr)
	{
		LOG("AVDecoderFFMPEG::init_geometry_context - no embedded geometry stream");
		return false;
	}
	LOG(
		"AVDecoderFFMPEG::init_geometry_context - Geometry Stream: %d",
		m_geometry_stream_index);
	return true;
}
bool AVDecoderFFMPEG::init_ffmpeg_context(const char* filepath)
{
	const bool remote_source =
		filepath != nullptr &&
		is_http_url(filepath);
	LOG(
		"AVDecoderFFMPEG::init_ffmpeg_context - file: %s",
		remote_source ? "<remote HTTP source>" : filepath);

	if (!m_container->open(filepath))
	{
		LOG("AVDecoderFFMPEG::init_ffmpeg_context - %s",
			m_container->error().c_str());
		return false;
	}
	return true;
}
bool AVDecoderFFMPEG::init_video_context()
{
	// Find Video Stream
	int stream = m_container->stream_index(
		openvolumetric::StreamKind::Video);
	if (stream < 0)
	{
		LOG("AVDecoderFFMPEG::init_video_context - video stream not found");
		return false;
	}
	else
	{
		//set stream index
		m_video_stream_index = stream;

		// Get Video Stream
		m_video_stream = m_container->native_stream(
			openvolumetric::StreamKind::Video);

		//Get Video Codec
		m_video_codec = avcodec_find_decoder(m_video_stream->codecpar->codec_id);
		if (m_video_codec == nullptr)
		{
			LOG("AVDecoderFFMPEG::init_video_context - video codec not available");
			return false;
		}

		//Get Video Codec Context
		m_video_codec_ctx = avcodec_alloc_context3(m_video_codec);
		if (m_video_codec_ctx == nullptr)
		{
			LOG("AVDecoderFFMPEG::init_video_context - could not allocate video codec context");
			return false;
		}

		// Copy the codec parameters to the codec context
		if (avcodec_parameters_to_context(m_video_codec_ctx, m_video_stream->codecpar) < 0)
		{
			LOG("AVDecoderFFMPEG::init_video_context - avcodec_parameters_to_context - error");
			return false;
		}

		// Decoder threading must be configured before avcodec_open2().
		// Setting these fields afterward leaves codecs such as HEVC using the
		// threading configuration chosen when the context was opened.
		m_video_codec_ctx->thread_count = 8;
		m_video_codec_ctx->thread_type =
			FF_THREAD_SLICE | FF_THREAD_FRAME;

		// Ready to decode ?
		if (avcodec_open2(m_video_codec_ctx, m_video_codec, nullptr) < 0)
		{
			LOG("AVDecoderFFMPEG::init_video_context - could not open video codec");
			return false;
		}

		// Populate video_info
		m_video_info.is_enabled			= true;
		m_video_info.width				= m_video_stream->codecpar->width;
		m_video_info.height				= m_video_stream->codecpar->height;
		// avg_frame_rate reflects the timestamps actually stored in the MP4.
		// r_frame_rate is only FFmpeg's guessed nominal rate and can be wrong
		// for HEVC streams, causing the engine clock to skip valid samples.
		const double average_fps =
			av_q2d(m_video_stream->avg_frame_rate);
		const double nominal_fps =
			av_q2d(m_video_stream->r_frame_rate);
		m_video_info.fps =
			std::isfinite(average_fps) && average_fps > 0.0
				? average_fps
				: nominal_fps;
		m_video_info.frame_count = static_cast<int>(std::clamp<std::int64_t>(
			m_video_stream->nb_frames,
			0,
			std::numeric_limits<int>::max()));

		// Calculate video duration
		double duration				= m_container->duration_seconds();
		m_video_info.total_time = std::max(
			duration,
			m_video_stream->duration <= 0
				? 0.0
				: m_video_stream->duration * av_q2d(m_video_stream->time_base));

		// Report video properties to log
		LOG("AVDecoderFFMPEG::init_video_context - Video Stream:  %d",		m_video_stream_index);
		LOG("AVDecoderFFMPEG::init_video_context - Size:          %d x %d", m_video_info.width, m_video_info.height);
		LOG("AVDecoderFFMPEG::init_video_context - FPS:           %f",		m_video_info.fps);
		LOG("AVDecoderFFMPEG::init_video_context - Nominal FPS:   %f",		nominal_fps);
		LOG("AVDecoderFFMPEG::init_video_context - Duration:      %f",		m_video_info.total_time);
		LOG("AVDecoderFFMPEG::init_video_context - Frame Count:   %d",		m_video_info.frame_count);
		LOG(
			"AVDecoderFFMPEG::init_video_context - Decoder Threads: %d "
			"(active type: %d)",
			m_video_codec_ctx->thread_count,
			m_video_codec_ctx->active_thread_type);

		//Done
		return true;
	}
}
bool AVDecoderFFMPEG::init(const char* filepath)
{
	m_video_decoder_drained = false;
	const bool remote_source =
		filepath != nullptr &&
		is_http_url(filepath);
	LOG(
		"AVDecoderFFMPEG::init - file: %s",
		remote_source ? "<remote HTTP source>" : filepath);

	// check if context is 
	if (this->m_initialised)
	{
		LOG("AVDecoderFFMPEG::init - decoder has already been init.");
		return true;
	}

	// check file path
	if (filepath == nullptr)
	{
		LOG("AVDecoderFFMPEG::init - filepath is nullptr.");
		return false;
	}

	// FFMPEG Init
	if (!init_ffmpeg_context(filepath))
	{
		return false;
	}

	// Video Context - required
	if (!init_video_context())
	{
		return false;
	}

	if (!init_audio_context())
	{
		return false;
	}

	if (!init_geometry_context())
	{
		return false;
	}

	// Set decoder init = true
	this->m_initialised = true;
	m_decoder_state = INITIALIZED;

	LOG("AVDecoderFFMPEG::init - end ");
	return true;
}
bool AVDecoderFFMPEG::start_decoding()
{
	if (!this->m_initialised)
	{
		LOG("AVDecoderFFMPEG::start_decoding - not INITIALIZED");
		return false;
	}
	if (m_thread_running.exchange(true, std::memory_order_acq_rel))
	{
		LOG("AVDecoderFFMPEG::start_decoding - already running");
		return true;
	}

	m_stop_requested.store(false, std::memory_order_release);

	// Publish ownership before constructing the worker. A caller may seek as
	// soon as start_decoding() returns; advertising the worker only from inside
	// its lambda left a window where seek() mutated FFmpeg directly while the
	// new worker was beginning to decode.
	try
	{
		m_decode_thread = std::thread([this]()
		{
			m_decoder_state = DECODING;

			while (!m_stop_requested.load(std::memory_order_acquire))
			{
				if (!process_seek_request())
				{
					m_decoder_state = STOP;
					break;
				}

				// Decode until stopped, drained, or waiting at end of stream.
				switch (m_decoder_state)
				{
				case DECODING:
				{
					if (!this->decode())
					{
						m_decoder_state = m_container->end_of_stream()
							? DECODE_EOF
							: STOP;
						if (m_decoder_state == STOP)
						{
							LOG(
								"AVDecoderFFMPEG::start_decoding - decoder stopped: %s",
								get_last_error().c_str());
							m_stop_requested.store(
								true, std::memory_order_release);
						}
					}
					break;
				}
				case DECODE_EOF:
				{
					// Keep tail frames available. The engine clock either stops or
					// submits a seek when looping is enabled.
					std::this_thread::sleep_for(
						std::chrono::milliseconds(1));
					break;
				}
				default:
					std::this_thread::yield();
					break;
				}
			}

			m_thread_running.store(false, std::memory_order_release);
			m_seek_condition.notify_all();
			LOG("AVDecoderFFMPEG::start_decoding - thread end");
		});
	}
	catch (const std::exception& exception)
	{
		m_thread_running.store(false, std::memory_order_release);
		m_seek_condition.notify_all();
		LOG(
			"AVDecoderFFMPEG::start_decoding - worker creation failed: %s",
			exception.what());
		return false;
	}

	return true;
}
bool AVDecoderFFMPEG::stop_decoding()
{
	LOG("AVDecoderFFMPEG::stop_decoding");

	m_stop_requested.store(true, std::memory_order_release);
	m_seek_condition.notify_all();

	// join main thread
	if (m_decode_thread.joinable()) 
	{
		m_decode_thread.join();
	}
	m_decoder_state = STOP;

	return true;
}

void AVDecoderFFMPEG::cancel_pending_io()
{
	m_container->cancel_pending_io();
}

ByteSourceDiagnostics AVDecoderFFMPEG::source_diagnostics() const
{
	return m_container->source_diagnostics();
}
bool AVDecoderFFMPEG::decode()
{
	if (!m_initialised)
	{
		LOG("DecoderFFMPEG::decode - not initialized");
		return false;
	}

	// Drain audio first so a full video or geometry output queue does not
	// prevent already-demuxed audio packets from reaching the PCM ring.
	if (!drain_pending_packets())
		return false;

	if (m_deferred_packet.has_value())
	{
		openvolumetric::ContainerPacket packet =
			std::move(*m_deferred_packet);
		m_deferred_packet.reset();
		if (!route_packet(std::move(packet)))
			return false;
		if (m_deferred_packet.has_value())
		{
			std::this_thread::yield();
			return true;
		}
	}

	openvolumetric::ContainerPacket packet;
	if (!m_container->read(packet))
	{
		if (m_container->end_of_stream())
		{
			if (!m_pending_video_packets.empty() ||
				!m_pending_audio_packets.empty() ||
				!m_pending_geometry_packets.empty())
			{
				return true;
			}
			if (!m_video_decoder_drained)
			{
				if (!drain_video_decoder())
					return false;
				m_video_decoder_drained = true;
				return true;
			}
			m_video_frames.mark_end_of_stream();
			m_geometry_frames.mark_end_of_stream();
			return false;
		}
		m_video_frames.set_error(m_container->error());
		m_geometry_frames.set_error(m_container->error());
		LOG("AVDecoderFFMPEG::decode - %s",
			m_container->error().c_str());
		return false;
	}

	return route_packet(std::move(packet));
}

bool AVDecoderFFMPEG::route_packet(
	openvolumetric::ContainerPacket packet)
{
	constexpr std::size_t max_pending_video = 64;
	constexpr std::size_t max_pending_audio = 64;
	constexpr std::size_t max_pending_geometry = 128;

	std::deque<openvolumetric::ContainerPacket>* pending = nullptr;
	bool blocked = false;
	std::size_t capacity = 0;
	switch (packet.kind)
	{
	case openvolumetric::StreamKind::Video:
		pending = &m_pending_video_packets;
		blocked = m_video_frames.full() || !pending->empty();
		capacity = max_pending_video;
		break;
	case openvolumetric::StreamKind::Audio:
		pending = &m_pending_audio_packets;
		blocked = !audio_can_accept_packet() || !pending->empty();
		capacity = max_pending_audio;
		break;
	case openvolumetric::StreamKind::Geometry:
		pending = &m_pending_geometry_packets;
		blocked = m_geometry_frames.full() || !pending->empty();
		capacity = max_pending_geometry;
		break;
	default:
		return true;
	}

	if (!blocked)
		return decode_packet(packet);
	if (pending->size() < capacity)
	{
		pending->push_back(std::move(packet));
		return true;
	}

	// Preserve the packet at the demux head until its consumer makes room.
	m_deferred_packet = std::move(packet);
	return true;
}

bool AVDecoderFFMPEG::drain_pending_packets()
{
	auto drain_one = [this](
		std::deque<openvolumetric::ContainerPacket>& packets,
		bool ready)
	{
		if (!ready || packets.empty())
			return true;
		openvolumetric::ContainerPacket packet =
			std::move(packets.front());
		packets.pop_front();
		return decode_packet(packet);
	};

	if (!drain_one(m_pending_audio_packets, audio_can_accept_packet()))
		return false;
	if (!drain_one(m_pending_video_packets, !m_video_frames.full()))
		return false;
	return drain_one(
		m_pending_geometry_packets, !m_geometry_frames.full());
}

bool AVDecoderFFMPEG::decode_packet(
	const openvolumetric::ContainerPacket& packet)
{
	av_packet_unref(&m_packet);
	if (av_new_packet(
		&m_packet, static_cast<int>(packet.payload.size())) < 0)
	{
		m_video_frames.set_error("Could not allocate a decoder packet.");
		m_geometry_frames.set_error("Could not allocate a decoder packet.");
		return false;
	}
	std::copy(packet.payload.begin(), packet.payload.end(), m_packet.data);
	m_packet.stream_index = packet.stream_index;
	m_packet.pts = packet.has_pts ? packet.pts : AV_NOPTS_VALUE;
	m_packet.dts = packet.has_dts ? packet.dts : AV_NOPTS_VALUE;
	m_packet.duration = packet.duration;

	bool success = true;
	switch (packet.kind)
	{
	case openvolumetric::StreamKind::Video:
		success = decode_video_frame();
		break;
	case openvolumetric::StreamKind::Audio:
		success = decode_audio_frame();
		break;
	case openvolumetric::StreamKind::Geometry:
		success = queue_geometry_packet();
		break;
	default:
		break;
	}
	av_packet_unref(&m_packet);
	return success;
}

bool AVDecoderFFMPEG::queue_geometry_packet()
{
	openvolumetric::GeometryPacket packet;
	if (!openvolumetric::parse_geometry_packet(
		m_packet.data,
		static_cast<std::size_t>(m_packet.size),
		packet))
	{
		const std::string error = "Malformed VVGF geometry packet.";
		m_geometry_frames.set_error(error);
		LOG("AVDecoderFFMPEG::queue_geometry_packet - %s", error.c_str());
		return false;
	}

	if (m_packet.pts == AV_NOPTS_VALUE)
	{
		const std::string error = "Geometry packet has no presentation timestamp.";
		m_geometry_frames.set_error(error);
		LOG("AVDecoderFFMPEG::queue_geometry_packet - %s", error.c_str());
		return false;
	}

	EncodedGeometryFrame frame;
	frame.generation =
		m_playback_generation.load(std::memory_order_acquire);
	frame.presentation_time =
		static_cast<double>(m_packet.pts) *
		av_q2d(m_geometry_stream->time_base);
	if (std::abs(frame.presentation_time - m_last_geometry_packet_time) <
		1e-9)
	{
		LOG(
			"SYNC duplicate geometry timestamp pts=%f",
			frame.presentation_time);
	}
	m_last_geometry_packet_time = frame.presentation_time;
	frame.source_frame_number = packet.frame_number;
	frame.packet = std::move(packet);

	if (!m_geometry_frames.try_push(std::move(frame)))
	{
		const std::string error = "Geometry packet queue is full.";
		m_geometry_frames.set_error(error);
		LOG("AVDecoderFFMPEG::queue_geometry_packet - %s", error.c_str());
		return false;
	}
	return true;
}

bool AVDecoderFFMPEG::has_embedded_geometry() const
{
	return m_geometry_stream_index >= 0;
}

bool AVDecoderFFMPEG::get_geometry_data(
	double presentation_time,
	EncodedGeometryFrame& output)
{
	return m_geometry_frames.access([&](auto& frames)
	{
		if (frames.empty() ||
			frames.front().presentation_time > presentation_time)
			return false;
		output = std::move(frames.front());
		frames.pop_front();
		return true;
	});
}

bool AVDecoderFFMPEG::geometry_end_of_stream() const
{
	return m_geometry_frames.state() ==
			openvolumetric::QueueState::EndOfStream &&
		m_geometry_frames.size() == 0;
}

std::string AVDecoderFFMPEG::get_last_error() const
{
	if (!m_container->error().empty())
		return m_container->error();
	if (m_video_frames.state() == openvolumetric::QueueState::Error)
		return m_video_frames.error();
	if (m_geometry_frames.state() == openvolumetric::QueueState::Error)
		return m_geometry_frames.error();
	return {};
}
// Decode and resample one audio packet into the lock-free PCM ring.
bool AVDecoderFFMPEG::decode_audio_frame()
{
	int ret = avcodec_send_packet(m_audio_codec_ctx, &m_packet);
	if (ret < 0 && ret != AVERROR(EAGAIN))
	{
		char error[AV_ERROR_MAX_STRING_SIZE]{};
		av_strerror(ret, error, sizeof(error));
		LOG(
			"AVDecoderFFMPEG::decode_audio_frame - send failed: %s",
			error);
		return false;
	}

	while (ret >= 0)
	{
		AVFrame* frame = av_frame_alloc();
		if (frame == nullptr)
			return false;

		ret = avcodec_receive_frame(m_audio_codec_ctx, frame);
		if (ret == 0)
		{
			const int output_capacity = static_cast<int>(av_rescale_rnd(
				swr_get_delay(
					m_audio_resampler, m_audio_codec_ctx->sample_rate) +
					frame->nb_samples,
				m_audio_info.sample_rate,
				m_audio_codec_ctx->sample_rate,
				AV_ROUND_UP));
			std::vector<float> converted(
				static_cast<size_t>(output_capacity) *
				static_cast<size_t>(m_audio_info.channels));
			uint8_t* output =
				reinterpret_cast<uint8_t*>(converted.data());
			const int output_samples = swr_convert(
				m_audio_resampler,
				&output,
				output_capacity,
				const_cast<const uint8_t**>(frame->extended_data),
				frame->nb_samples);
			if (output_samples > 0)
			{
				std::size_t sample_offset = 0;
				if (m_audio_discard_before >= 0.0 &&
					frame->best_effort_timestamp != AV_NOPTS_VALUE)
				{
					const double frame_time =
						static_cast<double>(frame->best_effort_timestamp) *
						av_q2d(m_audio_stream->time_base);
					const double frame_end =
						frame_time +
						static_cast<double>(output_samples) /
							static_cast<double>(m_audio_info.sample_rate);
					if (frame_end <= m_audio_discard_before)
					{
						sample_offset =
							static_cast<std::size_t>(output_samples) *
							static_cast<std::size_t>(m_audio_info.channels);
					}
					else
					{
						if (frame_time < m_audio_discard_before)
						{
							const double seconds_to_skip =
								m_audio_discard_before - frame_time;
							const std::size_t frames_to_skip =
								static_cast<std::size_t>(std::ceil(
									seconds_to_skip *
									m_audio_info.sample_rate));
							sample_offset = std::min(
								frames_to_skip *
									static_cast<std::size_t>(
										m_audio_info.channels),
								static_cast<std::size_t>(output_samples) *
									static_cast<std::size_t>(
										m_audio_info.channels));
						}
						m_audio_discard_before = -1.0;
					}
				}
				const std::size_t converted_count =
					static_cast<std::size_t>(output_samples) *
					static_cast<std::size_t>(m_audio_info.channels);
				if (!push_audio(
					converted.data() + sample_offset,
					converted_count - sample_offset))
				{
					m_video_frames.set_error(
						"Audio PCM ring has insufficient capacity.");
					m_geometry_frames.set_error(
						"Audio PCM ring has insufficient capacity.");
					LOG(
						"AVDecoderFFMPEG::decode_audio_frame - PCM ring full "
						"(samples=%zu)",
						converted_count - sample_offset);
					av_frame_free(&frame);
					return false;
				}
			}
			av_frame_free(&frame);
		}
		else
		{
			av_frame_free(&frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			char error[AV_ERROR_MAX_STRING_SIZE]{};
			av_strerror(ret, error, sizeof(error));
			LOG(
				"AVDecoderFFMPEG::decode_audio_frame - receive failed: %s",
				error);
			return false;
		}
	}
	return true;
}

bool AVDecoderFFMPEG::push_audio(
	const float* samples, size_t sample_count)
{
	if (samples == nullptr || sample_count == 0 || m_audio_samples.empty())
		return true;

	const uint64_t write =
		m_audio_write_position.load(std::memory_order_relaxed);
	const uint64_t read =
		m_audio_read_position.load(std::memory_order_acquire);
	const uint64_t available =
		static_cast<uint64_t>(m_audio_samples.size()) - (write - read);
	if (sample_count > available)
		return false;
	for (size_t i = 0; i < sample_count; ++i)
		m_audio_samples[(write + i) % m_audio_samples.size()] = samples[i];
	m_audio_write_position.store(write + sample_count, std::memory_order_release);
	return true;
}

bool AVDecoderFFMPEG::audio_can_accept_packet() const
{
	if (m_audio_codec_ctx == nullptr || m_audio_samples.empty())
		return true;
	const uint64_t write =
		m_audio_write_position.load(std::memory_order_acquire);
	const uint64_t read =
		m_audio_read_position.load(std::memory_order_acquire);
	const uint64_t available =
		static_cast<uint64_t>(m_audio_samples.size()) - (write - read);
	// A one-second reserve comfortably exceeds a decoded AAC packet and keeps
	// the demux thread from silently dropping PCM when the audio callback lags.
	const uint64_t reserve =
		static_cast<uint64_t>(m_audio_info.sample_rate) *
		static_cast<uint64_t>(m_audio_info.channels);
	return available >= reserve;
}

int AVDecoderFFMPEG::read_audio(float* output, int sample_count)
{
	if (output == nullptr || sample_count <= 0)
		return 0;

	const uint64_t read =
		m_audio_read_position.load(std::memory_order_relaxed);
	const uint64_t write =
		m_audio_write_position.load(std::memory_order_acquire);
	const int count = static_cast<int>(
		std::min<uint64_t>(
			static_cast<uint64_t>(sample_count), write - read));
	for (int i = 0; i < count; ++i)
		output[i] = m_audio_samples[(read + i) % m_audio_samples.size()];
	if (count < sample_count)
	{
		std::fill(output + count, output + sample_count, 0.0f);
		m_audio_underrun_count.fetch_add(1, std::memory_order_relaxed);
	}
	m_audio_read_position.store(read + count, std::memory_order_release);
	return count;
}

IAVDecoder::AudioBufferInfo AVDecoderFFMPEG::audio_buffer_info() const
{
	AudioBufferInfo result;
	if (!m_audio_info.is_enabled ||
		m_audio_info.sample_rate <= 0 ||
		m_audio_info.channels <= 0)
		return result;

	const uint64_t read =
		m_audio_read_position.load(std::memory_order_acquire);
	const uint64_t write =
		m_audio_write_position.load(std::memory_order_acquire);
	const uint64_t origin =
		m_audio_timeline_origin_position.load(std::memory_order_acquire);
	const double samples_per_second =
		static_cast<double>(m_audio_info.sample_rate) *
		static_cast<double>(m_audio_info.channels);
	result.read_time =
		m_audio_timeline_origin_time.load(std::memory_order_acquire) +
		static_cast<double>(read - origin) / samples_per_second;
	result.buffered_duration =
		static_cast<double>(write - read) / samples_per_second;
	result.underrun_count =
		m_audio_underrun_count.load(std::memory_order_relaxed);
	return result;
}

void AVDecoderFFMPEG::flush_audio()
{
	// The Unity audio callback is the consumer and can be in read_audio()
	// while the decoder thread seeks. Resetting both monotonic positions to
	// zero lets an in-flight callback publish an old, much larger read value
	// afterward. That underflows write-read and permanently blocks the shared
	// demux thread. Discard buffered PCM by advancing read to the current
	// write position without ever moving the producer position backwards.
	const uint64_t write =
		m_audio_write_position.load(std::memory_order_acquire);
	m_audio_read_position.store(write, std::memory_order_release);
}
bool AVDecoderFFMPEG::decode_video_frame()
{
	int send_result = avcodec_send_packet(m_video_codec_ctx, &m_packet);
	if (send_result == AVERROR(EAGAIN))
	{
		// FFmpeg still owns output from an earlier packet. Drain that output,
		// then retry this exact packet instead of silently dropping it.
		LOG("AVDecoderFFMPEG::decode_video_frame - recovering send EAGAIN");
		if (!receive_video_frames())
			return false;
		send_result = avcodec_send_packet(m_video_codec_ctx, &m_packet);
	}
	if (send_result < 0)
	{
		char error[AV_ERROR_MAX_STRING_SIZE]{};
		av_strerror(send_result, error, sizeof(error));
		m_video_frames.set_error(
			std::string("FFmpeg rejected a video packet: ") + error);
		LOG(
			"AVDecoderFFMPEG::decode_video_frame - send failed: %s",
			error);
		return false;
	}

	return receive_video_frames();
}

bool AVDecoderFFMPEG::receive_video_frames()
{
	while (true)
	{
		AVFrame* frame = av_frame_alloc();
		if (frame == nullptr)
		{
			m_video_frames.set_error(
				"Could not allocate a decoded video frame.");
			return false;
		}

		const int receive_result =
			avcodec_receive_frame(m_video_codec_ctx, frame);
		if (receive_result == AVERROR(EAGAIN) ||
			receive_result == AVERROR_EOF)
		{
			av_frame_free(&frame);
			return true;
		}
		if (receive_result < 0)
		{
			av_frame_free(&frame);
			char error[AV_ERROR_MAX_STRING_SIZE]{};
			av_strerror(receive_result, error, sizeof(error));
			m_video_frames.set_error(
				std::string("FFmpeg video receive failed: ") + error);
			LOG(
				"AVDecoderFFMPEG::receive_video_frames - failed: %s",
				error);
			return false;
		}

		FrameData framedata;
		framedata.data = frame;
		framedata.frame_time =
			av_q2d(m_video_stream->time_base) *
			static_cast<double>(frame->best_effort_timestamp);
		if (std::abs(
			framedata.frame_time - m_last_video_packet_time) < 1e-9)
		{
			LOG(
				"SYNC duplicate video timestamp pts=%f",
				framedata.frame_time);
		}
		m_last_video_packet_time = framedata.frame_time;

		if (!m_video_frames.try_push(std::move(framedata)))
		{
			av_frame_free(&frame);
			m_video_frames.set_error("Decoded video queue is full.");
			return false;
		}
	}
}

bool AVDecoderFFMPEG::drain_video_decoder()
{
	int send_result = avcodec_send_packet(m_video_codec_ctx, nullptr);
	if (send_result == AVERROR(EAGAIN))
	{
		if (!receive_video_frames())
			return false;
		send_result = avcodec_send_packet(m_video_codec_ctx, nullptr);
	}
	if (send_result < 0 && send_result != AVERROR_EOF)
	{
		char error[AV_ERROR_MAX_STRING_SIZE]{};
		av_strerror(send_result, error, sizeof(error));
		m_video_frames.set_error(
			std::string("FFmpeg video drain failed: ") + error);
		return false;
	}
	return receive_video_frames();
}
void AVDecoderFFMPEG::clean_frame_data()
{
	free_front_frame();
}
void AVDecoderFFMPEG::free_front_frame()
{
	m_video_frames.access([&](auto& frames)
	{
		if (m_initialised && !frames.empty())
		{
			av_frame_free(&frames.front().data);
			frames.pop_front();
		}
	});
}
// Delete all data in the buffers
void AVDecoderFFMPEG::flush_buffers()
{
	LOG(" AVDecoderFFMPEG::flush_buffers - start");

	//std::lock_guard<std::mutex> lock(m_video_mutex);

	m_video_frames.clear([](FrameData& frame)
	{
		av_frame_free(&frame.data);
	});

	LOG(" AVDecoderFFMPEG::flush_buffers - end");
}
bool AVDecoderFFMPEG::seek(double time)
{
	//Check decoder is init
	if (!m_initialised)
	{
		LOG("AVDecoderFFMPEG::seek - not initialized \n");
		return false;
	}

	if (!m_thread_running.load(std::memory_order_acquire))
		return perform_seek(time);

	std::unique_lock<std::mutex> lock(m_seek_mutex);
	const std::uint64_t request_id = ++m_seek_request_id;
	m_requested_seek = time;
	m_seek_condition.notify_all();
	m_seek_condition.wait(lock, [&]()
	{
		return m_completed_seek_id >= request_id ||
			!m_thread_running.load(std::memory_order_acquire);
	});
	return m_completed_seek_id >= request_id && m_seek_result;
}

bool AVDecoderFFMPEG::process_seek_request()
{
	std::optional<double> requested_time;
	std::uint64_t request_id = 0;
	{
		std::lock_guard<std::mutex> lock(m_seek_mutex);
		if (!m_requested_seek.has_value())
			return true;
		requested_time = m_requested_seek;
		m_requested_seek.reset();
		request_id = m_seek_request_id;
	}

	const bool result = perform_seek(*requested_time);
	{
		std::lock_guard<std::mutex> lock(m_seek_mutex);
		m_seek_result = result;
		m_completed_seek_id = request_id;
	}
	m_seek_condition.notify_all();
	return result;
}

bool AVDecoderFFMPEG::perform_seek(double time)
{
	if (!m_container->seek(time))
	{
		LOG("AVDecoderFFMPEG::seek - %s", m_container->error().c_str());
		return false;
	}

	avcodec_flush_buffers(m_video_codec_ctx);
	if (m_audio_codec_ctx != nullptr)
	{
		avcodec_flush_buffers(m_audio_codec_ctx);
		swr_close(m_audio_resampler);
		if (swr_init(m_audio_resampler) < 0)
		{
			m_video_frames.set_error(
				"Audio resampler could not be reset after seek.");
			m_geometry_frames.set_error(
				"Audio resampler could not be reset after seek.");
			return false;
		}
	}
	flush_buffers();
	m_geometry_frames.clear();
	m_pending_video_packets.clear();
	m_pending_audio_packets.clear();
	m_pending_geometry_packets.clear();
	m_deferred_packet.reset();
	flush_audio();
	const uint64_t audio_origin =
		m_audio_read_position.load(std::memory_order_acquire);
	m_audio_timeline_origin_position.store(
		audio_origin, std::memory_order_release);
	m_audio_timeline_origin_time.store(time, std::memory_order_release);
	m_audio_underrun_count.store(0, std::memory_order_release);
	m_audio_discard_before = time;
	m_playback_generation.fetch_add(1, std::memory_order_acq_rel);
	m_last_video_packet_time = -1.0;
	m_last_geometry_packet_time = -1.0;
	m_video_decoder_drained = false;
	m_decoder_state = DECODING;

	return true;
}

std::uint64_t AVDecoderFFMPEG::playback_generation() const
{
	return m_playback_generation.load(std::memory_order_acquire);
}
openvolumetric::FrameMatchResult AVDecoderFFMPEG::get_video_data(
	double presentation_time,
	double tolerance,
	double& actual_presentation_time,
	uint8_t** outputY,
	uint8_t** outputU,
	uint8_t** outputV)
{
//	LOG("DecoderFFMPEG::get_video_data - start");
	*outputY = nullptr;
	*outputU = nullptr;
	*outputV = nullptr;
	if (!m_initialised)
	{
		LOG("DecoderFFMPEG::get_video_data - decoder not initialized");
		return openvolumetric::FrameMatchResult::NotReady;
	}

	AVFrame* frame = m_video_frames.access([&](auto& frames) -> AVFrame*
	{
		while (!frames.empty())
		{
			if (frames.front().frame_time >= presentation_time - tolerance)
				break;
			av_frame_free(&frames.front().data);
			frames.pop_front();
		}
		// Dropping obsolete decoded frames is normal when the engine render
		// rate and encoded rate differ. Never log here: this executes on the
		// render thread and per-frame I/O creates a self-amplifying stall.
		return frames.empty() ? nullptr : frames.front().data;
	});

	// Check that frame has managed to be assigned 
	if (frame == nullptr)
	{
		if (m_video_frames.state() == openvolumetric::QueueState::Error)
			LOG("DecoderFFMPEG::get_video_data - %s",
				m_video_frames.error().c_str());
		return openvolumetric::FrameMatchResult::NotReady;
	}

	const double frame_time = m_video_frames.access([&](auto& frames)
	{
		return frames.empty() ? -1.0 : frames.front().frame_time;
	});
	if (frame_time > presentation_time + tolerance)
		return openvolumetric::FrameMatchResult::Missing;

	// get decoded frame
	*outputY = frame->data[0];
	*outputU = frame->data[1];
	*outputV = frame->data[2];

	int64_t timeStamp				= frame->best_effort_timestamp;
	double timeInSec				= av_q2d(m_video_stream->time_base) * timeStamp;
	this->m_video_info.last_time	= timeInSec;
	actual_presentation_time = frame_time;

//	LOG("DecoderFFMPEG::get_video_data - time in sec(s): %f", timeInSec);

	return openvolumetric::FrameMatchResult::Ready;
}

bool AVDecoderFFMPEG::copy_selected_video(
	std::vector<std::uint8_t>& y,
	std::vector<std::uint8_t>& u,
	std::vector<std::uint8_t>& v)
{
	return m_video_frames.access([&](auto& frames)
	{
		if (frames.empty() || frames.front().data == nullptr)
			return false;

		const AVFrame* frame = frames.front().data;
		const int widths[] = {
			m_video_info.width,
			(m_video_info.width + 1) / 2,
			(m_video_info.width + 1) / 2
		};
		const int heights[] = {
			m_video_info.height,
			(m_video_info.height + 1) / 2,
			(m_video_info.height + 1) / 2
		};
		std::vector<std::uint8_t>* outputs[] = {&y, &u, &v};
		for (int plane = 0; plane < 3; ++plane)
		{
			if (frame->data[plane] == nullptr ||
				frame->linesize[plane] == 0)
				return false;
			outputs[plane]->resize(
				static_cast<std::size_t>(widths[plane]) *
				static_cast<std::size_t>(heights[plane]));
			for (int row = 0; row < heights[plane]; ++row)
			{
				const std::uint8_t* source =
					frame->data[plane] +
					static_cast<std::ptrdiff_t>(row) *
						frame->linesize[plane];
				std::copy_n(
					source,
					widths[plane],
					outputs[plane]->data() +
						static_cast<std::size_t>(row) * widths[plane]);
			}
		}
		return true;
	});
}

} // namespace openvolumetric
