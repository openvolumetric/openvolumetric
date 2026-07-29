#include "OpenVolumetricPlayer.h"

#include <AVDecoderFFMPEG.h>
#include <GeometryDecoderDraco.h>

#include <cmath>
#include <utility>

namespace openvolumetric
{

class OpenVolumetricPlayer::Impl
{
public:
	AVDecoderFFMPEG media;
	GeometryDecoderDraco geometry;
	OpenVolumetricMediaInfo info;
	std::string error;
	std::uint64_t generation = 0;
	bool open = false;
	bool started = false;
	bool has_pending_video = false;
	double pending_video_time = 0.0;
};

OpenVolumetricPlayer::OpenVolumetricPlayer() : m_impl(std::make_unique<Impl>())
{
}

OpenVolumetricPlayer::~OpenVolumetricPlayer()
{
	close();
}

bool OpenVolumetricPlayer::open(const char* path)
{
	close();
	if (path == nullptr || path[0] == '\0')
	{
		m_impl->error = "An OpenVolumetric MP4 path is required.";
		return false;
	}
	if (!m_impl->media.init(path))
	{
		m_impl->error = m_impl->media.get_last_error();
		m_impl->media.destroy();
		return false;
	}
	if (!m_impl->geometry.init())
	{
		m_impl->error = m_impl->geometry.get_last_error();
		m_impl->geometry.destroy();
		m_impl->media.destroy();
		return false;
	}

	const IAVDecoder::VideoInfo video = m_impl->media.get_video_info();
	const IAVDecoder::AudioInfo audio = m_impl->media.get_audio_info();
	m_impl->info.width = video.width;
	m_impl->info.height = video.height;
	m_impl->info.frame_rate = video.fps;
	m_impl->info.duration = video.total_time;
	m_impl->info.has_audio = audio.is_enabled;
	m_impl->info.audio_sample_rate = audio.sample_rate;
	m_impl->info.audio_channels = audio.channels;
	m_impl->generation = m_impl->media.playback_generation();
	m_impl->error.clear();
	m_impl->open = true;
	return true;
}

bool OpenVolumetricPlayer::start()
{
	if (!m_impl->open)
		return false;
	if (m_impl->started)
		return true;
	if (!m_impl->media.start_decoding() ||
		!m_impl->geometry.start_decoding())
	{
		m_impl->error = "OpenVolumetric decoder workers could not start.";
		m_impl->geometry.stop_decoding();
		m_impl->media.stop_decoding();
		return false;
	}
	m_impl->started = true;
	return true;
}

void OpenVolumetricPlayer::stop()
{
	if (!m_impl->started)
		return;
	m_impl->media.stop_decoding();
	m_impl->geometry.stop_decoding();
	m_impl->started = false;
}

void OpenVolumetricPlayer::close()
{
	if (!m_impl)
		return;
	stop();
	// Both destroy methods are deliberately idempotent so partial open and
	// startup failures follow the same rollback path as normal shutdown.
	m_impl->geometry.destroy();
	m_impl->media.destroy();
	m_impl->open = false;
	m_impl->info = {};
	m_impl->generation = 0;
	m_impl->has_pending_video = false;
	m_impl->pending_video_time = 0.0;
}

bool OpenVolumetricPlayer::seek(double time)
{
	if (!m_impl->open || time < 0.0 || !std::isfinite(time))
		return false;
	if (!m_impl->media.seek(time))
	{
		m_impl->error = m_impl->media.get_last_error();
		return false;
	}
	m_impl->generation = m_impl->media.playback_generation();
	m_impl->geometry.reset(m_impl->generation);
	m_impl->has_pending_video = false;
	return true;
}

const OpenVolumetricMediaInfo& OpenVolumetricPlayer::media_info() const
{
	return m_impl->info;
}

const std::string& OpenVolumetricPlayer::error() const
{
	return m_impl->error;
}

bool OpenVolumetricPlayer::submit_geometry(double requested_time)
{
	const std::uint64_t generation = m_impl->media.playback_generation();
	if (generation != m_impl->generation)
	{
		m_impl->generation = generation;
		m_impl->geometry.reset(generation);
	}

	constexpr double lookahead = 4.0;
	IAVDecoder::EncodedGeometryFrame encoded;
	while (m_impl->geometry.can_accept_encoded_frame() &&
		m_impl->media.get_geometry_data(
			requested_time + lookahead, encoded))
	{
		if (!m_impl->geometry.submit_encoded_frame(
			encoded.generation,
			encoded.presentation_time,
			std::move(encoded.packet)))
			return false;
	}
	if (m_impl->media.geometry_end_of_stream())
		m_impl->geometry.mark_end_of_stream(generation);
	return true;
}

FrameMatchResult OpenVolumetricPlayer::presentation(
	double requested_time,
	OpenVolumetricPresentation& output)
{
	if (!m_impl->started || !submit_geometry(requested_time))
		return FrameMatchResult::NotReady;

	const double tolerance = m_impl->info.frame_rate > 0.0
		? (0.5 / m_impl->info.frame_rate) + 0.0001
		: 0.017;
	std::uint8_t* y = nullptr;
	std::uint8_t* u = nullptr;
	std::uint8_t* v = nullptr;
	double video_time = 0.0;
	const double video_target = m_impl->has_pending_video
		? m_impl->pending_video_time
		: requested_time;
	const FrameMatchResult video_result = m_impl->media.get_video_data(
		video_target, tolerance, video_time, &y, &u, &v);
	if (video_result != FrameMatchResult::Ready)
		return video_result;
	if (!m_impl->has_pending_video)
	{
		m_impl->has_pending_video = true;
		m_impl->pending_video_time = video_time;
	}

	Mesh mesh;
	double geometry_time = 0.0;
	const FrameMatchResult geometry_result =
		m_impl->geometry.get_mesh_data(
			video_time, tolerance, geometry_time, mesh);
	if (geometry_result == FrameMatchResult::Missing)
	{
		static_cast<IAVDecoder&>(m_impl->media).clean_frame_data();
		m_impl->has_pending_video = false;
	}
	if (geometry_result != FrameMatchResult::Ready)
		return geometry_result;

	OpenVolumetricPresentation candidate;
	candidate.presentation_time = video_time;
	candidate.width = m_impl->info.width;
	candidate.height = m_impl->info.height;
	candidate.mesh = std::move(mesh);
	if (!m_impl->media.copy_selected_video(
		candidate.y, candidate.u, candidate.v))
		return FrameMatchResult::NotReady;

	static_cast<IAVDecoder&>(m_impl->media).clean_frame_data();
	m_impl->geometry.clear_frame_data();
	m_impl->has_pending_video = false;
	output = std::move(candidate);
	return FrameMatchResult::Ready;
}

int OpenVolumetricPlayer::read_audio(float* output, int sample_count)
{
	return m_impl->open
		? m_impl->media.read_audio(output, sample_count)
		: 0;
}

} // namespace openvolumetric
