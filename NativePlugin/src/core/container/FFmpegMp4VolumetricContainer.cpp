#include "FFmpegMp4VolumetricContainer.h"

extern "C"
{
#include <libavutil/error.h>
}

#include <array>
#include <cmath>

namespace volumetric_video
{
namespace
{

constexpr std::uint32_t make_tag(char a, char b, char c, char d)
{
	return static_cast<std::uint32_t>(a) |
		(static_cast<std::uint32_t>(b) << 8) |
		(static_cast<std::uint32_t>(c) << 16) |
		(static_cast<std::uint32_t>(d) << 24);
}

constexpr std::uint32_t kGeometryTag = make_tag('v', 'v', 'g', 'e');

std::string ffmpeg_error(int value)
{
	std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
	av_strerror(value, buffer.data(), buffer.size());
	return buffer.data();
}

std::size_t kind_slot(StreamKind kind)
{
	switch (kind)
	{
	case StreamKind::Video: return 0;
	case StreamKind::Audio: return 1;
	case StreamKind::Geometry: return 2;
	default: return 0;
	}
}

} // namespace

FFmpegMp4VolumetricContainer::FFmpegMp4VolumetricContainer()
	: m_context(nullptr),
	  m_stream_indices{{-1, -1, -1}},
	  m_end_of_stream(false)
{
}

FFmpegMp4VolumetricContainer::~FFmpegMp4VolumetricContainer()
{
	close();
}

bool FFmpegMp4VolumetricContainer::open(const char* path)
{
	close();
	if (path == nullptr || path[0] == '\0')
	{
		set_error("Container path is empty.");
		return false;
	}

	int result = avformat_open_input(&m_context, path, nullptr, nullptr);
	if (result < 0)
	{
		set_error("Could not open MP4: " + ffmpeg_error(result));
		return false;
	}
	result = avformat_find_stream_info(m_context, nullptr);
	if (result < 0)
	{
		set_error("Could not read MP4 stream information: " +
			ffmpeg_error(result));
		close();
		return false;
	}
	if (!discover_streams())
	{
		const std::string discovery_error = m_error;
		close();
		m_error = discovery_error;
		return false;
	}
	m_end_of_stream = false;
	m_error.clear();
	return true;
}

void FFmpegMp4VolumetricContainer::close()
{
	if (m_context != nullptr)
		avformat_close_input(&m_context);
	m_stream_indices = {{-1, -1, -1}};
	m_end_of_stream = false;
}

bool FFmpegMp4VolumetricContainer::discover_streams()
{
	std::array<int, 3> counts{{0, 0, 0}};
	for (unsigned int index = 0; index < m_context->nb_streams; ++index)
	{
		const AVCodecParameters* parameters =
			m_context->streams[index]->codecpar;
		StreamKind kind = StreamKind::Unknown;
		if (parameters->codec_type == AVMEDIA_TYPE_VIDEO)
			kind = StreamKind::Video;
		else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO)
			kind = StreamKind::Audio;
		else if (parameters->codec_type == AVMEDIA_TYPE_DATA &&
			parameters->codec_tag == kGeometryTag)
			kind = StreamKind::Geometry;

		if (kind == StreamKind::Unknown)
			continue;
		const std::size_t slot = kind_slot(kind);
		++counts[slot];
		if (m_stream_indices[slot] < 0)
			m_stream_indices[slot] = static_cast<int>(index);
	}

	if (counts[0] == 0)
		set_error("MP4 is missing its video track.");
	else if (counts[0] > 1)
		set_error("MP4 contains multiple video tracks; exactly one is required.");
	else if (counts[2] == 0)
		set_error("MP4 is missing its vvge geometry track.");
	else if (counts[2] > 1)
		set_error("MP4 contains multiple vvge geometry tracks.");
	else if (counts[1] > 1)
		set_error("MP4 contains multiple audio tracks; at most one is supported.");
	else
		return true;
	return false;
}

bool FFmpegMp4VolumetricContainer::read(ContainerPacket& output)
{
	if (m_context == nullptr)
	{
		set_error("Cannot read from a closed container.");
		return false;
	}

	AVPacket packet{};
	const int result = av_read_frame(m_context, &packet);
	if (result == AVERROR_EOF)
	{
		m_end_of_stream = true;
		m_error.clear();
		return false;
	}
	if (result < 0)
	{
		set_error("Failed to read MP4 packet: " + ffmpeg_error(result));
		return false;
	}

	output = {};
	output.stream_index = packet.stream_index;
	output.pts = packet.pts;
	output.dts = packet.dts;
	output.duration = packet.duration;
	output.has_pts = packet.pts != AV_NOPTS_VALUE;
	output.has_dts = packet.dts != AV_NOPTS_VALUE;
	const AVRational time_base =
		m_context->streams[packet.stream_index]->time_base;
	output.time_base = {time_base.num, time_base.den};
	if (packet.size > 0 && packet.data != nullptr)
		output.payload.assign(packet.data, packet.data + packet.size);

	if (packet.stream_index == m_stream_indices[0])
		output.kind = StreamKind::Video;
	else if (packet.stream_index == m_stream_indices[1])
		output.kind = StreamKind::Audio;
	else if (packet.stream_index == m_stream_indices[2])
		output.kind = StreamKind::Geometry;

	av_packet_unref(&packet);
	return true;
}

bool FFmpegMp4VolumetricContainer::seek(double seconds)
{
	if (m_context == nullptr || !std::isfinite(seconds) || seconds < 0.0)
	{
		set_error("Seek requires an open container and a non-negative time.");
		return false;
	}
	const std::int64_t timestamp = static_cast<std::int64_t>(
		std::llround(seconds * static_cast<double>(AV_TIME_BASE)));
	const int result = av_seek_frame(
		m_context, -1, timestamp, AVSEEK_FLAG_BACKWARD);
	if (result < 0)
	{
		set_error("MP4 seek failed: " + ffmpeg_error(result));
		return false;
	}
	avformat_flush(m_context);
	m_end_of_stream = false;
	m_error.clear();
	return true;
}

bool FFmpegMp4VolumetricContainer::is_open() const
{
	return m_context != nullptr;
}

bool FFmpegMp4VolumetricContainer::end_of_stream() const
{
	return m_end_of_stream;
}

const std::string& FFmpegMp4VolumetricContainer::error() const
{
	return m_error;
}

int FFmpegMp4VolumetricContainer::stream_index(StreamKind kind) const
{
	return m_stream_indices[kind_slot(kind)];
}

AVStream* FFmpegMp4VolumetricContainer::native_stream(StreamKind kind) const
{
	const int index = stream_index(kind);
	return m_context != nullptr && index >= 0 ? m_context->streams[index] : nullptr;
}

const AVCodecParameters* FFmpegMp4VolumetricContainer::codec_parameters(
	StreamKind kind) const
{
	AVStream* stream = native_stream(kind);
	return stream == nullptr ? nullptr : stream->codecpar;
}

double FFmpegMp4VolumetricContainer::duration_seconds() const
{
	return m_context == nullptr || m_context->duration <= 0
		? 0.0
		: static_cast<double>(m_context->duration) / AV_TIME_BASE;
}

void FFmpegMp4VolumetricContainer::set_error(const std::string& message)
{
	m_error = message;
	m_end_of_stream = false;
}

} // namespace volumetric_video
