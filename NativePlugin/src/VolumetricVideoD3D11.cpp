#include "VolumetricVideoD3D11.h"

#include <iostream>

#include <Logger.h>
#include <AVDecoderFFMPEG.h>
#include <TextureD3D11.h>


//----------------------------------------------
//
VolumetricVideoD3D11::VolumetricVideoD3D11(int ID) : IVolumetricVideo(ID)
{
	LOG("VolumetricVideoD3D11::VolumetricVideoD3D11 - Constructor - %d", ID);
	
	// Create AVDecoder
	this->m_avdecoder	= new AVDecoderFFMPEG();

	// Create Texture
	this->m_texture		= new TextureD3D11();


}

//----------------------------------------------
// 
VolumetricVideoD3D11::~VolumetricVideoD3D11() 
{
	LOG("VolumetricVideoD3D11::~VolumetricVideoD3D11 - id: %d ", this->m_id);

	delete this->m_avdecoder;
	delete this->m_texture;
}

//----------------------------------------------
//
bool VolumetricVideoD3D11::set_video(const char* filepath)
{
	LOG("VolumetricVideoD3D11::set_video - id: %d file: %s", this->m_id, filepath);

	//
	return this->m_avdecoder->init(filepath);
}



//----------------------------------------------
//
int VolumetricVideoD3D11::seek(double time)
{
	LOG("VolumetricVideoD3D11::seek - id: %d time: %f", this->m_id, time);

	//
	if (!this->m_avdecoder->seek(time))
	{
		return -1;
	}

	//
	return 1;
}


//--------------------------------------------------------
//
int VolumetricVideoD3D11::start()
{
	LOG("VolumetricVideoD3D11::start - id: %d", this->m_id);


	if (m_avdecoder == NULL)
	{
		LOG("VolumetricVideoD3D11::start - id: %d - m_avdecoder==NULL", this->m_id);
		return -1;
	}


	// Start Decoding - Starts threading
	if (!this->m_avdecoder->start_decoding())
	{
		return -1;
	}

	//
	return 1;
}


//--------------------------------------------------------
//
int VolumetricVideoD3D11::stop()
{
	LOG("VolumetricVideoD3D11::stop - id: %d", this->m_id);


	// Stop Decoding 
	if (!this->m_avdecoder->stop_decoding())
	{
		return -1;
	}


	//
	return 1;
}


//----------------------------------------------
//
int VolumetricVideoD3D11::render()
{
	LOG("VolumetricVideoD3D11::render - id: %d", this->m_id);

	// Points to texture data
	uint8_t * outputY = NULL;
	uint8_t * outputU = NULL;
	uint8_t * outputV = NULL;

	// Get Frame Data 
	int frame_index = this->m_avdecoder->get_video_data(&outputY, &outputU, &outputV);
	if (frame_index == -1)
	{
		LOG("VolumetricVideoD3D11::render - id: %d - no buffer data - frame: %d", this->m_id, frame_index);
		return -1;
	}

	//Upload video data to texture 
	this->m_texture->upload(outputY, outputU, outputV);
	



	// Clean frame data from the most recently rendered frame
	this->m_avdecoder->clean_frame_data();

	//
	return frame_index;
}
