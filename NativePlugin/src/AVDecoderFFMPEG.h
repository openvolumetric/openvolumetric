#pragma once

#include <IAVDecoder.h>

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswresample/swresample.h>
}

#include <mutex>
#include <queue>
#include <thread>



class AVDecoderFFMPEG : public IAVDecoder
{

public:
	
	//----------------------------------
	//
	AVDecoderFFMPEG();
	

	//----------------------------------
	//
	~AVDecoderFFMPEG();


	//----------------------------------
	//
	bool init(const char* filename);


	// --------------------------------------------------------------------------
	// Start Decoding
	//
	bool start_decoding();

	// --------------------------------------------------------------------------
	// Stop Decoding
	//
	bool stop_decoding();


	//----------------------------------
	//
	bool decode();


	//----------------------------------
	//
	bool seek(double time);


	//----------------------------------
	//
	bool get_video_data(uint8_t** outputY, uint8_t** outputU, uint8_t** outputV);

protected:

	//----------------------------------
	//
	bool init_ffmpeg_context(const char* filepath);

	//----------------------------------
	//
	bool init_video_context();

	//----------------------------------
	//
	bool is_buffer_blocked();

	//----------------------------------
	//
	void update_buffer_state();

	//----------------------------------
	//
	bool decode_video_frame();

	//----------------------------------
	//
	void clean_frame_data();


private:

	void free_front_frame(std::queue<AVFrame*>* buffer, std::mutex* mutex);



private:

	//----------------------------------
	// AVFormat Context
	//----------------------------------
	AVFormatContext*		m_avformat_ctx;

	//----------------------------------
	// Video Information
	//----------------------------------
	int						m_video_stream_index;
	AVStream*				m_video_stream;
	AVCodecContext*			m_video_codec_ctx;
	AVCodec*				m_video_codec;

	//----------------------------------
	// Decoding and Buffers
	//----------------------------------
	AVPacket				m_packet;
	std::queue<AVFrame*>	m_video_frames;
	unsigned int			m_video_buffer_max;

	//----------------------------------
	// Threading 
	//----------------------------------
	std::thread				m_decode_thread;
	std::mutex				m_video_mutex;

};



