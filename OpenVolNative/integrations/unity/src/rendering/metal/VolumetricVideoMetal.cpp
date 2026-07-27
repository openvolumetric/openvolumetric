#include "VolumetricVideoMetal.h"

#include <AVDecoderFFMPEG.h>
#include <GeometryDecoderDraco.h>
#include <Logger.h>
#include <MeshBufferMetal.h>
#include <TextureMetal.h>

#include <cmath>

namespace openvol::unity
{

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
	if (!submit_embedded_geometry(m_presentation_time))
		return -1;

	const double fps = m_avdecoder->get_video_info().fps;
	const double tolerance = fps > 0.0 ? (0.5 / fps) + 0.0001 : 0.017;
	double video_time = 0.0;
	uint8_t* output_y = nullptr;
	uint8_t* output_u = nullptr;
	uint8_t* output_v = nullptr;
	if (m_avdecoder->get_video_data(
		m_presentation_time,
		tolerance,
		video_time,
		&output_y,
		&output_u,
		&output_v) != openvol::FrameMatchResult::Ready)
		return -1;

	Mesh mesh;
	double geometry_time = 0.0;
	const auto geometry_result = m_geometrydecoder->get_mesh_data(
		video_time, tolerance, geometry_time, mesh);
	if (geometry_result == openvol::FrameMatchResult::Missing)
	{
		LOG("SYNC dropping unmatched video pts=%f", video_time);
		m_avdecoder->clean_frame_data();
		return -1;
	}
	if (geometry_result != openvol::FrameMatchResult::Ready)
		return -1;

	// Present texture and geometry from the same frame as one render event.
	// Until both decoders have caught up, keep displaying the previous frame.
	m_texture->upload(output_y, output_u, output_v);
	if (!m_meshbuffer->update(&mesh))
		return -1;

	m_avdecoder->clean_frame_data();
	m_geometrydecoder->clear_frame_data();
	return static_cast<int>(std::llround(video_time * fps));
}

int VolumetricVideoMetal::seek(double time)
{
	if (!m_avdecoder->seek(time))
		return -1;
	m_geometrydecoder->reset(m_avdecoder->playback_generation());
	m_geometry_generation = m_avdecoder->playback_generation();
	if (m_geometrydecoder->get_decoder_state() == IDecoder::DECODING &&
		!prepare_presentation(time))
	{
		return -1;
	}
	return 1;
}

} // namespace openvol::unity
