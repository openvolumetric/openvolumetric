#include "AVDecoderFFMPEG.h"

#include <Logger.h>


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
AVDecoderFFMPEG::AVDecoderFFMPEG() : IAVDecoder()
{
	// AVContext
	m_avformat_ctx			= NULL;

	// Video
	m_video_stream_index	= -1;
	m_video_stream			= NULL;
	m_video_codec_ctx		= NULL;
	m_video_codec			= NULL;

	// Buffer Sizes
	m_video_buffer_max		= 32;

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

	//Free all Context variables
	if (m_avformat_ctx != NULL)
	{
		avformat_close_input(&m_avformat_ctx);
		avformat_free_context(m_avformat_ctx);
		m_avformat_ctx = NULL;
	}

	//Video Variables
	if (m_video_codec_ctx != NULL)
	{
		avcodec_free_context(&m_video_codec_ctx);
	}
	//
	m_video_codec = NULL;
	m_video_stream = NULL;

	//
	av_packet_unref(&m_packet);

	// Other Variables
	this->m_decoder_state = STOP;

	//
	LOG("AVDecoderFFMPEG::destroy - stop");
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::init_ffmpeg_context(const char* filepath)
{
	LOG("AVDecoderFFMPEG::init_ffmpeg_context - file: %s", filepath);

	// Create AV Format Context
	if (m_avformat_ctx == NULL)
	{
		m_avformat_ctx = avformat_alloc_context();
	}

	// Open Filename
	int ret = avformat_open_input(&m_avformat_ctx, filepath, NULL, NULL);
	if (ret < 0)
	{
		LOG("AVDecoderFFMPEG::init - avformat_open_input error(%x)", ret);
		return false;
	}

	// Get Stream information
	ret = avformat_find_stream_info(m_avformat_ctx, NULL);
	if (ret < 0)
	{
		LOG("DecoderFFMPEG::init_ffmpeg_context - avformat_find_stream_info error(%x)", ret);
		return false;
	}

	// FFMPEG init
	return true;
}



// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::init_video_context()
{
	// Find Video Stream
	int stream = av_find_best_stream(m_avformat_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
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
		m_video_stream = m_avformat_ctx->streams[m_video_stream_index];

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
		m_video_info.fps				= av_q2d(m_avformat_ctx->streams[m_video_stream_index]->r_frame_rate);
		m_video_info.frame_count		= m_video_stream->nb_frames;

		// Calculate video duration
		double duration				= (double)(m_avformat_ctx->duration) / AV_TIME_BASE;
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
		// Read Packet
		if (av_read_frame(m_avformat_ctx, &m_packet) < 0)
		{
			LOG("DecoderFFMPEG::decode - Failed to read packet");
			return false;
		}

		//Decode Video Frame
		if (m_video_info.is_enabled && m_packet.stream_index == m_video_stream->index)
		{
//			LOG("DecoderFFMPEG::decode - calling - decode_video_frame");
			decode_video_frame();
		}
	
		// Unref current packet 
		av_packet_unref(&m_packet);
	}


	//
	return true;
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

			// Lock and Push
			std::lock_guard<std::mutex> lock(m_video_mutex);
			m_video_frames.push(framedata);
						
			//LOG("DecoderFFMPEG::decode_video_frame - m_video_frames: %d", m_video_frames.size());
			//LOG("DecoderFFMPEG::decode_video_frame - decoded frame: %d", frame_index);
		}

		// Error or End of File 
		else if (ret == AVERROR(EAGAIN))
		{
			//char error_str[64];
			//av_make_error_string(error_str, 64, ret);
			//LOG("DecoderFFMPEG::decode_video_frame - ERROR: %s", error_str);
			break;
		}
	}

	// Update buffer state while the consumer cannot mutate the queue.
	{
		std::lock_guard<std::mutex> lock(m_video_mutex);
		update_buffer_state();
	}

	//
	return true;
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
void AVDecoderFFMPEG::clean_frame_data()
{
	free_front_frame(&m_video_frames, &m_video_mutex);
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
void AVDecoderFFMPEG::free_front_frame(std::queue<FrameData>* buffer, std::mutex* mutex)
{
	std::lock_guard<std::mutex> lock(*mutex);
	if (!m_initialised || buffer->size() == 0)
	{
		return;
	}

	//
	FrameData* frame = &buffer->front();
	av_frame_free(&frame->data);
	buffer->pop();
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
	while(!m_video_frames.empty())
	{
		clean_frame_data();
	}

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

	// compute timestamp
	uint64_t timeStamp = (uint64_t)time * AV_TIME_BASE;

	//seek to frame
	if (av_seek_frame(m_avformat_ctx, -1, timeStamp, AVSEEK_FLAG_BACKWARD) < 0)
	{
		LOG("AVDecoderFFMPEG::seek - seek time fail");
		return false;
	}

	//
	return true;
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
bool AVDecoderFFMPEG::get_video_data(int frame_index, uint8_t** outputY, uint8_t** outputU, uint8_t** outputV)
{
//	LOG("DecoderFFMPEG::get_video_data - start");
	std::lock_guard<std::mutex> lock(m_video_mutex);
	if (!m_initialised || m_video_frames.empty())
	{
		LOG("DecoderFFMPEG::get_video_data - buffer empty");
		*outputY = NULL;
		*outputU = NULL;
		*outputV = NULL;
		return false;
	}

	// TODO - Need to undersatand how best to handle these cases - particularly when the frame requested is too far infront/behind
	AVFrame* frame = NULL;
	while (!m_video_frames.empty())
	{
		// Case 1: Front of buffer is the requested frame
		if (m_video_frames.front().frame_index == frame_index)
		{
			frame = m_video_frames.front().data;
			break;
		}

		// Case 2: requested frame is greater than front frame index 
		else if(frame_index > m_video_frames.front().frame_index )
		{
			FrameData* old_frame = &m_video_frames.front();
			av_frame_free(&old_frame->data);
			m_video_frames.pop();
			update_buffer_state();
		}

		// Case 3: requested frame is less than the buffer 
		else if (frame_index < m_video_frames.front().frame_index)
		{
			// Do nothing as requested frame will catch up
			break;
		}
		// Should not occur
		else
		{

		}
	}

	// Check that frame has managed to be assigned 
	if (frame == NULL)
	{
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

	std::lock_guard<std::mutex> lock(m_video_mutex);
	return m_video_info.is_enabled && m_video_frames.size() >= m_video_buffer_max;
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
		if (m_video_frames.size() == 0)
		{
			m_video_info.buffer_state = BufferState::EMPTY;
		}
		else if (m_video_frames.size() >= m_video_buffer_max)
		{
			m_video_info.buffer_state = BufferState::FULL;
		}
		else
		{
			m_video_info.buffer_state = BufferState::NORMAL;
		}
	}
}
