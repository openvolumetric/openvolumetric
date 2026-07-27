#include "VolumetricVideoVulkan.h"

#include <AVDecoderFFMPEG.h>
#include <GeometryDecoderDraco.h>
#include <Logger.h>
#include <MeshBufferVulkan.h>
#include <TextureVulkan.h>

#include <cmath>

namespace openvol::unity
{

VolumetricVideoVulkan::VolumetricVideoVulkan(int id)
    : IVolumetricVideo(id)
{
    LOG("VolumetricVideoVulkan::VolumetricVideoVulkan - %d", id);
    m_avdecoder = new AVDecoderFFMPEG();
    m_texture = new TextureVulkan();
    m_geometrydecoder = new GeometryDecoderDraco();
    m_meshbuffer = new MeshBufferVulkan();
}


VolumetricVideoVulkan::~VolumetricVideoVulkan() = default;

void VolumetricVideoVulkan::destroy()
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

int VolumetricVideoVulkan::start()
{
    if (m_avdecoder == nullptr || m_geometrydecoder == nullptr)
        return -1;
    if (!m_avdecoder->start_decoding() ||
        !m_geometrydecoder->start_decoding())
        return -1;
    return 1;
}

int VolumetricVideoVulkan::stop()
{
    if (!m_avdecoder->stop_decoding() ||
        !m_geometrydecoder->stop_decoding())
        return -1;
    return 1;
}

int VolumetricVideoVulkan::render()
{
    if (!submit_embedded_geometry(m_presentation_time))
        return -1;

    const double fps = m_avdecoder->get_video_info().fps;
    const double tolerance =
        fps > 0.0 ? (0.5 / fps) + 0.0001 : 0.017;
    double video_time = 0.0;
    std::uint8_t* y = nullptr;
    std::uint8_t* u = nullptr;
    std::uint8_t* v = nullptr;
    const double video_target =
        m_has_pending_video ? m_pending_video_time : m_presentation_time;
    if (m_avdecoder->get_video_data(
            video_target,
            tolerance,
            video_time,
            &y,
            &u,
            &v) != openvol::FrameMatchResult::Ready)
        return -1;
    if (!m_has_pending_video)
    {
        m_has_pending_video = true;
        m_pending_video_time = video_time;
    }

    Mesh mesh;
    double geometry_time = 0.0;
    const auto geometry_result = m_geometrydecoder->get_mesh_data(
        video_time, tolerance, geometry_time, mesh);
    if (geometry_result == openvol::FrameMatchResult::Missing)
    {
        LOG("SYNC dropping unmatched video pts=%f", video_time);
        m_avdecoder->clean_frame_data();
        m_has_pending_video = false;
        return -1;
    }
    if (geometry_result != openvol::FrameMatchResult::Ready)
        return -1;

    m_texture->upload(y, u, v);
    if (!m_meshbuffer->update(&mesh))
        return -1;

    m_avdecoder->clean_frame_data();
    m_geometrydecoder->clear_frame_data();
    m_has_pending_video = false;
    set_last_presented_time(video_time);
    return static_cast<int>(std::llround(video_time * fps));
}

int VolumetricVideoVulkan::seek(double time)
{
    m_has_pending_video = false;
    if (!m_avdecoder->seek(time))
        return -1;
    m_geometrydecoder->reset(m_avdecoder->playback_generation());
    m_geometry_generation = m_avdecoder->playback_generation();
    if (m_geometrydecoder->get_decoder_state() == IDecoder::DECODING &&
        !prepare_presentation(time))
        return -1;
    return 1;
}

} // namespace openvol::unity
