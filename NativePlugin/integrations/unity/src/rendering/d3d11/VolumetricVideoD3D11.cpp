#include "VolumetricVideoD3D11.h"

#include <Logger.h>
#include <AVDecoderFFMPEG.h>
#include <TextureD3D11.h>
#include <MeshBufferD3D11.h>
#include <GeometryDecoderDraco.h>
#include <Mesh.h>

#include <iostream>
#include <cmath>

//----------------------------------------------
// 
//----------------------------------------------
VolumetricVideoD3D11::VolumetricVideoD3D11(int ID) : IVolumetricVideo(ID)
{
	LOG("VolumetricVideoD3D11::VolumetricVideoD3D11 - Constructor - %d", ID);
	
	// Create AVDecoder
	this->m_avdecoder		= new AVDecoderFFMPEG();

	// Create Texture
	this->m_texture			= new TextureD3D11();
 
	// Create Geometry Decoeder
	this->m_geometrydecoder = new GeometryDecoderDraco();

	// Create Mesh Buffer
	this->m_meshbuffer		= new MeshBufferD3D11();
}

//----------------------------------------------
// 
//----------------------------------------------
VolumetricVideoD3D11::~VolumetricVideoD3D11() 
{
	LOG("VolumetricVideoD3D11::~VolumetricVideoD3D11 - id: %d ", this->m_id);
}


//----------------------------------------------
// 
//----------------------------------------------
void VolumetricVideoD3D11::destroy()
{
	LOG("VolumetricVideoD3D11::destroy - start id: %d ", this->m_id);

	//  Clean up texture resources
	this->m_texture->destroy();
	delete this->m_texture;

	// Clean up avdecoder resources
	this->m_avdecoder->destroy();
	delete this->m_avdecoder;

	this->m_geometrydecoder->destroy();
	delete this->m_geometrydecoder;
	
	this->m_meshbuffer->destroy();
	delete this->m_meshbuffer;
	
	LOG("VolumetricVideoD3D11::destroy - end id: %d ", this->m_id);

}




//----------------------------------------------
// 
//----------------------------------------------
int VolumetricVideoD3D11::start()
{
	LOG("VolumetricVideoD3D11::start - id: %d", this->m_id);

	// Check decoder is init
	if (m_avdecoder == NULL)
	{
		LOG("VolumetricVideoD3D11::start - id: %d - m_avdecoder==NULL", this->m_id);
		return -1;
	}

	// Check decoder is init
	if (m_geometrydecoder == NULL)
	{
		LOG("VolumetricVideoD3D11::start - id: %d - m_geometrydecoder==NULL", this->m_id);
		return -1;
	}

	// Start Decoding - Starts threading
	if (!this->m_avdecoder->start_decoding())
	{
		return -1;
	}

	// Start Decoding - Starts threading
	if (!this->m_geometrydecoder->start_decoding())
	{
		return -1;
	}
	
	//
	return 1;
}


//----------------------------------------------
// 
//----------------------------------------------
int VolumetricVideoD3D11::stop()
{
	LOG("VolumetricVideoD3D11::stop - id: %d", this->m_id);

	// Stop decoding textures
	if (!this->m_avdecoder->stop_decoding())
	{
		return -1;
	}
	
	// Stop decoding geometry
	if (!this->m_geometrydecoder->stop_decoding())
	{
		return -1;
	}

	//
	return 1;
}


//----------------------------------------------
// 
//----------------------------------------------
int VolumetricVideoD3D11::render()
{
//	LOG("VolumetricVideoD3D11::render - id: %d", this->m_id);

	if (!submit_embedded_geometry(m_presentation_time))
		return -1;

	const double fps = m_avdecoder->get_video_info().fps;
	const double tolerance = fps > 0.0 ? (0.5 / fps) + 0.0001 : 0.017;
	double video_time = 0.0;

	// Pointers to texture data
	uint8_t * outputY = NULL;
	uint8_t * outputU = NULL;
	uint8_t * outputV = NULL;

	// Get Frame Data 
	if (this->m_avdecoder->get_video_data(
		m_presentation_time,
		tolerance,
		video_time,
		&outputY,
		&outputU,
		&outputV) != volumetric_video::FrameMatchResult::Ready)
	{
		return -1;
	}

	Mesh mesh;
	double geometry_time = 0.0;
	const auto geometry_result = this->m_geometrydecoder->get_mesh_data(
		video_time, tolerance, geometry_time, mesh);
	if (geometry_result == volumetric_video::FrameMatchResult::Missing)
	{
		LOG("SYNC dropping unmatched video pts=%f", video_time);
		this->m_avdecoder->clean_frame_data();
		return -1;
	}
	if (geometry_result != volumetric_video::FrameMatchResult::Ready)
	{
		return -1;
	}

	// Present matching texture and geometry in the same render event.
	this->m_texture->upload(outputY, outputU, outputV);
	if (!this->m_meshbuffer->update(&mesh))
	{
		return -1;
	}

	// Clean frame data from the most recently rendered frame
	this->m_avdecoder->clean_frame_data();
	this->m_geometrydecoder->clear_frame_data();

	// Return rendered frame index
	return static_cast<int>(std::llround(video_time * fps));
}



//----------------------------------------------
// 
//----------------------------------------------
int VolumetricVideoD3D11::seek(double time)
{
	LOG("VolumetricVideoD3D11::seek - id: %d time: %f", this->m_id, time);

	//
	if (!this->m_avdecoder->seek(time))
	{
		return -1;
	}
	this->m_geometrydecoder->reset(
		this->m_avdecoder->playback_generation());
	m_geometry_generation = this->m_avdecoder->playback_generation();
	if (this->m_geometrydecoder->get_decoder_state() ==
			IDecoder::DECODING &&
		!prepare_presentation(time))
	{
		return -1;
	}

	//
	return 1;
}
