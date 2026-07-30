#include "UnityOpenVolumetricPlayer.h"

#include <cmath>
#include <utility>

namespace openvolumetric::unity
{

UnityOpenVolumetricPlayer::UnityOpenVolumetricPlayer(
	int id,
	std::unique_ptr<ITexture> texture,
	std::unique_ptr<IMeshBuffer> mesh_buffer)
	: m_id(id),
	  m_texture(std::move(texture)),
	  m_mesh_buffer(std::move(mesh_buffer))
{
}

UnityOpenVolumetricPlayer::~UnityOpenVolumetricPlayer()
{
	close();
}

bool UnityOpenVolumetricPlayer::open(const char* path)
{
	m_last_presented_time.store(-1.0, std::memory_order_release);
	return m_player.open(path);
}

bool UnityOpenVolumetricPlayer::start()
{
	return m_player.start();
}

bool UnityOpenVolumetricPlayer::stop()
{
	m_player.stop();
	return true;
}

bool UnityOpenVolumetricPlayer::seek(double time)
{
	if (!m_player.seek(time))
		return false;
	m_last_presented_time.store(-1.0, std::memory_order_release);
	return true;
}

void UnityOpenVolumetricPlayer::close()
{
	m_player.close();
	if (m_texture)
		m_texture->destroy();
	if (m_mesh_buffer)
		m_mesh_buffer->destroy();
	m_last_presented_time.store(-1.0, std::memory_order_release);
}

void UnityOpenVolumetricPlayer::set_presentation_time(double time)
{
	m_presentation_time.store(time, std::memory_order_release);
}

int UnityOpenVolumetricPlayer::render()
{
	OpenVolumetricPresentation presentation;
	const FrameMatchResult result = m_player.presentation(
		m_presentation_time.load(std::memory_order_acquire),
		presentation);
	if (result != FrameMatchResult::Ready)
		return -1;

	m_texture->upload(
		presentation.y.data(),
		presentation.u.data(),
		presentation.v.data());
	if (!m_mesh_buffer->update(&presentation.mesh))
		return -1;

	m_last_presented_time.store(
		presentation.presentation_time,
		std::memory_order_release);
	const double fps = m_player.media_info().frame_rate;
	return fps > 0.0
		? static_cast<int>(std::llround(
			presentation.presentation_time * fps))
		: 0;
}

const OpenVolumetricMediaInfo& UnityOpenVolumetricPlayer::media_info() const
{
	return m_player.media_info();
}

OpenVolumetricBufferInfo UnityOpenVolumetricPlayer::buffer_info() const
{
	return m_player.buffer_info();
}

OpenVolumetricAudioBufferInfo UnityOpenVolumetricPlayer::audio_buffer_info() const
{
	return m_player.audio_buffer_info();
}

const std::string& UnityOpenVolumetricPlayer::error() const
{
	return m_player.error();
}

int UnityOpenVolumetricPlayer::read_audio(float* output, int sample_count)
{
	return m_player.read_audio(output, sample_count);
}

double UnityOpenVolumetricPlayer::last_presented_time() const
{
	return m_last_presented_time.load(std::memory_order_acquire);
}

} // namespace openvolumetric::unity
