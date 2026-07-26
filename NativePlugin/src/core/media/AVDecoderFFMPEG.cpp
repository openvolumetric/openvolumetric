#include "AVDecoderFFMPEG.h"

#include <Logger.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
AVDecoderFFMPEG::AVDecoderFFMPEG()
	: IAVDecoder(),
	  m_container(new volumetric_video::FFmpegMp4VolumetricContainer()),
	  m_video_frames(32),
	  m_geometry_frames(128)
{
	// Video
	m_video_stream_index	= -1;
	m_video_stream			= NULL;
	m_video_codec_ctx		= NULL;
	m_video_codec			= NULL;

	// Audio
	m_audio_stream_index	= -1;
	m_audio_stream			= NULL;
	m_audio_codec_ctx		= NULL;
	m_audio_codec			= NULL;
	m_audio_resampler		= NULL;
	m_audio_read_position.store(0);
	m_audio_write_position.store(0);

	// Embedded geometry
	m_geometry_stream_index = -1;
	m_geometry_stream = NULL;

	// Initialise the packet without the removed av_init_packet API.
	m_packet = {};
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
AVDecoderFFMPEG::~AVDecoderFFMPEG()
{
	//
	LOG("AVDecoderFFMPEG::~AVDecoderFFMPEG" );
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
void AVDecoderFFMPEG::destroy()
{
	//
	LOG("AVDecoderFFMPEG::destroy - start");

	// Delete Buffer Data
	flush_buffers();

	m_container->close();

	//Video Variables
	if (m_video_codec_ctx != NULL)
	{
		avcodec_free_context(&m_video_codec_ctx);
	}
	//
	m_video_codec = NULL;
	m_video_stream = NULL;

	if (m_audio_resampler != NULL)
	{
		swr_free(&m_audio_resampler);
	}
	if (m_audio_codec_ctx != NULL)
	{
		avcodec_free_context(&m_audio_codec_ctx);
	}
	m_audio_codec = NULL;
	m_audio_stream = NULL;
	flush_audio();
	m_geometry_frames.clear();
	m_geometry_stream = NULL;
	m_geometry_stream_index = -1;

	//
	av_packet_unref(&m_packet);

	// Other Variables
	this->m_decoder_state = STOP;

	//
	LOG("AVDecoderFFMPEG::destroy - stop");
}

// --------------------------------------------------------------------------
// Initialise the optional audio stream and convert it to interleaved stereo
// float samples suitable for Unity's streaming AudioClip callback.
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::init_audio_context()
{
	int stream = m_container->stream_index(
		volumetric_video::StreamKind::Audio);
	if (stream < 0)
	{
		LOG("AVDecoderFFMPEG::init_audio_context - no audio stream");
		return true;
	}

	m_audio_stream_index = stream;
	m_audio_stream = m_container->native_stream(
		volumetric_video::StreamKind::Audio);
	m_audio_codec = avcodec_find_decoder(m_audio_stream->codecpar->codec_id);
	if (m_audio_codec == NULL)
	{
		LOG("AVDecoderFFMPEG::init_audio_context - audio codec not available");
		return false;
	}

	m_audio_codec_ctx = avcodec_alloc_context3(m_audio_codec);
	if (m_audio_codec_ctx == NULL ||
		avcodec_parameters_to_context(
			m_audio_codec_ctx, m_audio_stream->codecpar) < 0 ||
		avcodec_open2(m_audio_codec_ctx, m_audio_codec, NULL) < 0)
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
		NULL);
	if (ret < 0 || m_audio_resampler == NULL ||
		swr_init(m_audio_resampler) < 0)
	{
		LOG("AVDecoderFFMPEG::init_audio_context - resampler init failed");
		return false;
	}

	m_audio_info.is_enabled = true;
	m_audio_info.sample_rate = sample_rate;
	m_audio_info.channels = stereo.nb_channels;
	m_audio_info.total_time =
		m_audio_stream->duration <= 0
			? m_container->duration_seconds()
			: m_audio_stream->duration * av_q2d(m_audio_stream->time_base);

	// Four seconds absorbs decoder and render-thread scheduling jitter.
	m_audio_samples.resize(
		static_cast<size_t>(sample_rate) *
		static_cast<size_t>(m_audio_info.channels) * 4u);
	m_audio_read_position.store(0, std::memory_order_relaxed);
	m_audio_write_position.store(0, std::memory_order_relaxed);

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
		volumetric_video::StreamKind::Geometry);
	m_geometry_stream = m_container->native_stream(
		volumetric_video::StreamKind::Geometry);
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


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::init_ffmpeg_context(const char* filepath)
{
	LOG("AVDecoderFFMPEG::init_ffmpeg_context - file: %s", filepath);

	if (!m_container->open(filepath))
	{
		LOG("AVDecoderFFMPEG::init_ffmpeg_context - %s",
			m_container->error().c_str());
		return false;
	}
	return true;
}



// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::init_video_context()
{
	// Find Video Stream
	int stream = m_container->stream_index(
		volumetric_video::StreamKind::Video);
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
			volumetric_video::StreamKind::Video);

		//Get Video Codec
		m_video_codec = avcodec_find_decoder(m_video_stream->codecpar->codec_id);
		if (m_video_codec == NULL)
		{
			LOG("AVDecoderFFMPEG::init_video_context - video codec not available");
			return false;
		}

		//Get Video Codec Context
		m_video_codec_ctx = avcodec_alloc_context3(m_video_codec);
		if (m_video_codec_ctx == NULL)
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

		// Ready to decode ?
		if (avcodec_open2(m_video_codec_ctx, m_video_codec, NULL) < 0)
		{
			LOG("AVDecoderFFMPEG::init_video_context - could not open video codec");
			return false;
		}

		//Decoder Thread settings
		m_video_codec_ctx->thread_count	= 8;
		m_video_codec_ctx->thread_type	= FF_THREAD_SLICE | FF_THREAD_FRAME;

		// Populate video_info
		m_video_info.is_enabled			= true;
		m_video_info.width				= m_video_stream->codecpar->width;
		m_video_info.height				= m_video_stream->codecpar->height;
		m_video_info.fps				= av_q2d(m_video_stream->r_frame_rate);
		m_video_info.frame_count		= m_video_stream->nb_frames;

		// Calculate video duration
		double duration				= m_container->duration_seconds();
		m_video_info.total_time		= m_video_stream->duration <= 0 ? duration : m_video_stream->duration * av_q2d(m_video_stream->time_base);

		// Report video properties to log
		LOG("AVDecoderFFMPEG::init_video_context - Video Stream:  %d",		m_video_stream_index);
		LOG("AVDecoderFFMPEG::init_video_context - Size:          %d x %d", m_video_info.width, m_video_info.height);
		LOG("AVDecoderFFMPEG::init_video_context - FPS:           %f",		m_video_info.fps);
		LOG("AVDecoderFFMPEG::init_video_context - Duration:      %f",		m_video_info.total_time);
		LOG("AVDecoderFFMPEG::init_video_context - Frame Count:   %f",		m_video_info.frame_count);

		//Done
		return true;
	}
}



// --------------------------------------------------------------------------
//
// TODO: ADD AUDIO CONTEXT
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::init(const char* filepath)
{
	LOG("AVDecoderFFMPEG::init - file: %s", filepath);

	// check if context is 
	if (this->m_initialised)
	{
		LOG("AVDecoderFFMPEG::init - decoder has already been init.");
		return true;
	}

	// check file path
	if (filepath == NULL)
	{
		LOG("AVDecoderFFMPEG::init - filepath is NULL.");
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

	// Done
	LOG("AVDecoderFFMPEG::init - end ");
	return true;
}

// --------------------------------------------------------------------------
// Start Decoding
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::start_decoding()
{
	if (!this->m_initialised)
	{
		LOG("AVDecoderFFMPEG::start_decoding - not INITIALIZED");
		return false;
	}

	// Create thread start video decoding
	m_decode_thread = std::thread([&]() 
	{
		// 
		m_decoder_state = DECODING;
		
		//
		while (m_decoder_state != STOP)
		{
			// Switch based on decoder state
			switch (m_decoder_state)
			{
			
			// If decoding
			case DECODING:
			{
				//LOG("AVDecoderFFMPEG::start_decoding - decoding");
				if (!this->decode())
				{
					m_decoder_state = DECODE_EOF;
				}
				break;
			}
			case SEEK:
			{
				LOG("AVDecoderFFMPEG::start_decoding - seek");
				// TODO Implement seek controls 
				break;
			}
			case DECODE_EOF:
			{
				LOG("AVDecoderFFMPEG::start_decoding - eof");
				this->seek(0.0);
				m_decoder_state = DECODING;
				break;
			}
			}
		}

		//
		LOG("AVDecoderFFMPEG::start_decoding - thread end");
	});

	//
	return true;
}

// --------------------------------------------------------------------------
// Stop Decoding
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::stop_decoding()
{
	//
	LOG("AVDecoderFFMPEG::stop_decoding");

	//
	m_decoder_state = STOP;

	// join main thread
	if (m_decode_thread.joinable()) 
	{
		m_decode_thread.join();
	}

	//
	return true;
}



// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::decode()
{
	//	LOG("DecoderFFMPEG::decode - start");

	// Check that the decoder is initialised 
	if (!m_initialised)
	{
		LOG("DecoderFFMPEG::decode - not initialized");
		return false;
	}

	//
	if (!is_buffer_blocked())
	{
		volumetric_video::ContainerPacket packet;
		if (!m_container->read(packet))
		{
			if (m_container->end_of_stream())
			{
				m_video_frames.mark_end_of_stream();
				m_geometry_frames.mark_end_of_stream();
			}
			else
			{
				m_video_frames.set_error(m_container->error());
				m_geometry_frames.set_error(m_container->error());
				LOG("AVDecoderFFMPEG::decode - %s",
					m_container->error().c_str());
			}
			return false;
		}

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
		if (packet.kind == volumetric_video::StreamKind::Video)
		{
			success = decode_video_frame();
		}
		else if (packet.kind == volumetric_video::StreamKind::Audio)
		{
			success = decode_audio_frame();
		}
		else if (packet.kind == volumetric_video::StreamKind::Geometry)
		{
			success = queue_geometry_packet();
		}
	
		av_packet_unref(&m_packet);
		if (!success)
			return false;
	}


	//
	return true;
}

bool AVDecoderFFMPEG::queue_geometry_packet()
{
	volumetric_video::GeometryPacket packet;
	if (!volumetric_video::parse_geometry_packet(
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
	frame.presentation_time =
		static_cast<double>(m_packet.pts) *
		av_q2d(m_geometry_stream->time_base);
	frame.frame_index = static_cast<int>(std::llround(
		frame.presentation_time * m_video_info.fps));
	frame.source_frame_number = packet.frame_number;
	frame.payload = std::move(packet.payload);

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
	int frame_index,
	EncodedGeometryFrame& output)
{
	return m_geometry_frames.access([&](auto& frames)
	{
		if (frames.empty() || frames.front().frame_index > frame_index)
			return false;
		output = std::move(frames.front());
		frames.pop_front();
		return true;
	});
}

std::string AVDecoderFFMPEG::get_last_error() const
{
	if (!m_container->error().empty())
		return m_container->error();
	if (m_video_frames.state() == volumetric_video::QueueState::Error)
		return m_video_frames.error();
	if (m_geometry_frames.state() == volumetric_video::QueueState::Error)
		return m_geometry_frames.error();
	return {};
}

// --------------------------------------------------------------------------
// Decode and resample one audio packet into the lock-free PCM ring.
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::decode_audio_frame()
{
	int ret = avcodec_send_packet(m_audio_codec_ctx, &m_packet);
	if (ret < 0 && ret != AVERROR(EAGAIN))
		return false;

	while (ret >= 0)
	{
		AVFrame* frame = av_frame_alloc();
		if (frame == NULL)
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
				push_audio(
					converted.data(),
					static_cast<size_t>(output_samples) *
						static_cast<size_t>(m_audio_info.channels));
			}
			av_frame_free(&frame);
		}
		else
		{
			av_frame_free(&frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			return false;
		}
	}
	return true;
}

void AVDecoderFFMPEG::push_audio(
	const float* samples, size_t sample_count)
{
	if (samples == NULL || sample_count == 0 || m_audio_samples.empty())
		return;

	const uint64_t write =
		m_audio_write_position.load(std::memory_order_relaxed);
	const uint64_t read =
		m_audio_read_position.load(std::memory_order_acquire);
	const uint64_t available =
		static_cast<uint64_t>(m_audio_samples.size()) - (write - read);
	const size_t count = static_cast<size_t>(
		std::min<uint64_t>(sample_count, available));
	for (size_t i = 0; i < count; ++i)
		m_audio_samples[(write + i) % m_audio_samples.size()] = samples[i];
	m_audio_write_position.store(write + count, std::memory_order_release);
}

int AVDecoderFFMPEG::read_audio(float* output, int sample_count)
{
	if (output == NULL || sample_count <= 0)
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
		std::fill(output + count, output + sample_count, 0.0f);
	m_audio_read_position.store(read + count, std::memory_order_release);
	return count;
}

void AVDecoderFFMPEG::flush_audio()
{
	m_audio_read_position.store(0, std::memory_order_relaxed);
	m_audio_write_position.store(0, std::memory_order_relaxed);
	m_audio_samples.clear();
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::decode_video_frame()
{
//	LOG("DecoderFFMPEG::decode_video_frame - start");

	//Send packet to decoder
	int ret = avcodec_send_packet(m_video_codec_ctx, &m_packet);

	// Get decoded frame
	while (ret >= 0)
	{
		//Allocate Frame
		AVFrame* frame = av_frame_alloc();

		// Get frame from decoder
		ret = avcodec_receive_frame(m_video_codec_ctx, frame);

		// Frame Decoded 
		if (ret == 0)
		{
			//Get frame index of the decoded frame
			int frame_index = (int)round(av_q2d(m_video_stream->time_base) * frame->best_effort_timestamp * m_video_info.fps);

			// Construct frame data
			FrameData framedata;
			framedata.data			= frame;
			framedata.frame_index	= frame_index;

			if (!m_video_frames.try_push(std::move(framedata)))
			{
				av_frame_free(&frame);
				m_video_frames.set_error("Decoded video queue is full.");
				return false;
			}
						
			//LOG("DecoderFFMPEG::decode_video_frame - m_video_frames: %d", m_video_frames.size());
			//LOG("DecoderFFMPEG::decode_video_frame - decoded frame: %d", frame_index);
		}

		// Error or End of File 
		else if (ret == AVERROR(EAGAIN))
		{
			av_frame_free(&frame);
			//char error_str[64];
			//av_make_error_string(error_str, 64, ret);
			//LOG("DecoderFFMPEG::decode_video_frame - ERROR: %s", error_str);
			break;
		}
		else
		{
			av_frame_free(&frame);
			return false;
		}
	}

	update_buffer_state();

	//
	return true;
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
void AVDecoderFFMPEG::clean_frame_data()
{
	free_front_frame();
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
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
	update_buffer_state();
}


// --------------------------------------------------------------------------
// Delete all data in the buffers
// --------------------------------------------------------------------------
void AVDecoderFFMPEG::flush_buffers()
{
	//
	LOG(" AVDecoderFFMPEG::flush_buffers - start");

	//
	//std::lock_guard<std::mutex> lock(m_video_mutex);

	//
	m_video_frames.clear([](FrameData& frame)
	{
		av_frame_free(&frame.data);
	});

	//
	LOG(" AVDecoderFFMPEG::flush_buffers - end");
}




// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::seek(double time)
{
	//Check decoder is init
	if (!m_initialised)
	{
		LOG("AVDecoderFFMPEG::seek - not initialized \n");
		return false;
	}

	if (!m_container->seek(time))
	{
		LOG("AVDecoderFFMPEG::seek - %s", m_container->error().c_str());
		return false;
	}

	avcodec_flush_buffers(m_video_codec_ctx);
	if (m_audio_codec_ctx != NULL)
		avcodec_flush_buffers(m_audio_codec_ctx);
	flush_buffers();
	m_geometry_frames.clear();

	//
	return true;
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::get_video_data(int frame_index, uint8_t** outputY, uint8_t** outputU, uint8_t** outputV)
{
//	LOG("DecoderFFMPEG::get_video_data - start");
	*outputY = NULL;
	*outputU = NULL;
	*outputV = NULL;
	if (!m_initialised)
	{
		LOG("DecoderFFMPEG::get_video_data - decoder not initialized");
		return false;
	}

	AVFrame* frame = m_video_frames.access([&](auto& frames) -> AVFrame*
	{
		while (!frames.empty())
		{
			if (frames.front().frame_index == frame_index)
				return frames.front().data;
			if (frame_index > frames.front().frame_index)
			{
				av_frame_free(&frames.front().data);
				frames.pop_front();
				continue;
			}
			break;
		}
		return nullptr;
	});
	update_buffer_state();

	// Check that frame has managed to be assigned 
	if (frame == NULL)
	{
		if (m_video_frames.state() == volumetric_video::QueueState::Error)
			LOG("DecoderFFMPEG::get_video_data - %s",
				m_video_frames.error().c_str());
		return false;
	}

	// get decoded frame
	*outputY = frame->data[0];
	*outputU = frame->data[1];
	*outputV = frame->data[2];

	//
	int64_t timeStamp				= frame->best_effort_timestamp;
	double timeInSec				= av_q2d(m_video_stream->time_base) * timeStamp;
	this->m_video_info.last_time	= timeInSec;

	// Convert time to frameindex
//	LOG("DecoderFFMPEG::get_video_data - requested frame_index: %d", frame_index);
//	LOG("DecoderFFMPEG::get_video_data - decoded frame_index:   %d", m_video_frames.front().frame_index);
//	LOG("DecoderFFMPEG::get_video_data - time in sec(s): %f", timeInSec);

	//
	return true;
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::is_buffer_blocked()
{
//	LOG("DecoderFFMPEG::is_buffer_blocked - start");

	return (m_video_info.is_enabled && m_video_frames.full()) ||
		m_geometry_frames.full();
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
void AVDecoderFFMPEG::update_buffer_state()
{
//	LOG("DecoderFFMPEG::update_buffer_state - start");

	// Update Video Buffer State
	if (m_video_info.is_enabled)
	{
		const std::size_t size = m_video_frames.size();
		if (size == 0)
		{
			m_video_info.buffer_state = BufferState::EMPTY;
		}
		else if (m_video_frames.full())
		{
			m_video_info.buffer_state = BufferState::FULL;
		}
		else
		{
			m_video_info.buffer_state = BufferState::NORMAL;
		}
	}
}
