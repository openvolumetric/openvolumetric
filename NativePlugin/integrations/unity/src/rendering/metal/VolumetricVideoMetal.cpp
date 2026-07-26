#include "VolumetricVideoMetal.h"

#include <AVDecoderFFMPEG.h>
#include <GeometryDecoderDraco.h>
#include <Logger.h>
#include <MeshBufferMetal.h>
#include <TextureMetal.h>

VolumetricVideoMetal::VolumetricVideoMetal(int ID) : IVolumetricVideo(ID)
{
	LOG("VolumetricVideoMetal::VolumetricVideoMetal - Constructor - %d", ID);
	m_avdecoder = new AVDecoderFFMPEG();
	m_texture = new TextureMetal();
	m_geometrydecoder = new GeometryDecoderDraco();
	m_meshbuffer = new MeshBufferMetal();
}

VolumetricVideoMetal::~VolumetricVideoMetal()
{
}

void VolumetricVideoMetal::destroy()
{
	if (m_texture != nullptr)
	{
		m_texture->destroy();
		delete m_texture;
		m_texture = nullptr;
	}
	if (m_avdecoder != nullptr)
	{
		m_avdecoder->destroy();
		delete m_avdecoder;
		m_avdecoder = nullptr;
	}
	if (m_geometrydecoder != nullptr)
	{
		m_geometrydecoder->destroy();
		delete m_geometrydecoder;
		m_geometrydecoder = nullptr;
	}
	if (m_meshbuffer != nullptr)
	{
		m_meshbuffer->destroy();
		delete m_meshbuffer;
		m_meshbuffer = nullptr;
	}
}

int VolumetricVideoMetal::start()
{
	if (m_avdecoder == nullptr || m_geometrydecoder == nullptr)
		return -1;
	if (!m_avdecoder->start_decoding() || !m_geometrydecoder->start_decoding())
		return -1;
	return 1;
}

int VolumetricVideoMetal::stop()
{
	if (!m_avdecoder->stop_decoding() || !m_geometrydecoder->stop_decoding())
		return -1;
	return 1;
}

int VolumetricVideoMetal::render()
{
	uint8_t* output_y = nullptr;
	uint8_t* output_u = nullptr;
	uint8_t* output_v = nullptr;
	if (!m_avdecoder->get_video_data(m_frame_index, &output_y, &output_u, &output_v))
		return -1;

	Mesh mesh;
	if (!m_geometrydecoder->get_mesh_data(m_frame_index, mesh))
		return -1;

	// Present texture and geometry from the same frame as one render event.
	// Until both decoders have caught up, keep displaying the previous frame.
	m_texture->upload(output_y, output_u, output_v);
	if (!m_meshbuffer->update(&mesh))
		return -1;

	m_avdecoder->clean_frame_data();
	m_geometrydecoder->clear_frame_data();
	return m_frame_index;
}

int VolumetricVideoMetal::seek(double time)
{
	return m_avdecoder->seek(time) ? 1 : -1;
}
