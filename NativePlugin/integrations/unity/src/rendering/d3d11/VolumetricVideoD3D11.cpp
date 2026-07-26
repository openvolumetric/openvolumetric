#include "VolumetricVideoD3D11.h"

#include <Logger.h>
#include <AVDecoderFFMPEG.h>
#include <TextureD3D11.h>
#include <MeshBufferD3D11.h>
#include <GeometryDecoderDraco.h>
#include <Mesh.h>

#include <iostream>

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

	// Pointers to texture data
	uint8_t * outputY = NULL;
	uint8_t * outputU = NULL;
	uint8_t * outputV = NULL;

	// Get Frame Data 
	if (!this->m_avdecoder->get_video_data(m_frame_index, &outputY, &outputU, &outputV))
	{
		LOG("VolumetricVideoD3D11::render - id: %d - no buffer data - frame: %d", this->m_id, m_frame_index);
		return -1;
	}

	// Wait until both streams have the requested frame.
	Mesh mesh;
	if (!this->m_geometrydecoder->get_mesh_data(m_frame_index, mesh))
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
	return m_frame_index;
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

	//
	return 1;
}
