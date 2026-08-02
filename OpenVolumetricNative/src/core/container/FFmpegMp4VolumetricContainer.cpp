#include "FFmpegMp4VolumetricContainer.h"

#include <HttpRangeByteSource.h>
#include <LocalFileByteSource.h>

extern "C"
{
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <array>
#include <cerrno>
#include <cmath>
#include <limits>
#include <memory>

namespace openvolumetric
{
namespace
{

/// Packs a four-character code using FFmpeg's tag byte order.
constexpr std::uint32_t make_tag(char a, char b, char c, char d)
{
	return static_cast<std::uint32_t>(a) |
		(static_cast<std::uint32_t>(b) << 8) |
		(static_cast<std::uint32_t>(c) << 16) |
		(static_cast<std::uint32_t>(d) << 24);
}

constexpr std::uint32_t kGeometryTag = make_tag('v', 'v', 'g', 'e');

/// Converts one FFmpeg status code into a readable diagnostic.
std::string ffmpeg_error(int value)
{
	std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
	av_strerror(value, buffer.data(), buffer.size());
	return buffer.data();
}

/// Maps the public stream kind to the fixed stream-index array slot.
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
	  m_io_context(nullptr),
	  m_stream_indices{{-1, -1, -1}},
	  m_duration_seconds(0.0),
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
	const std::string input(path);
	if (is_http_url(input))
	{
		auto source = std::make_unique<HttpRangeByteSource>(input);
		if (!source->is_open())
		{
			set_error(source->error());
			return false;
		}
		return open(std::move(source));
	}
	auto source = std::make_unique<LocalFileByteSource>(path);
	if (!source->is_open())
	{
		set_error(source->error());
		return false;
	}
	return open(std::move(source));
}

bool FFmpegMp4VolumetricContainer::open(
	std::unique_ptr<IByteSource> source)
{
	close();
	if (!source)
	{
		set_error("A byte source is required.");
		return false;
	}
	m_source = std::move(source);

	constexpr int io_buffer_size = 32 * 1024;
	std::uint8_t* io_buffer =
		static_cast<std::uint8_t*>(av_malloc(io_buffer_size));
	if (io_buffer == nullptr)
	{
		set_error("Could not allocate the FFmpeg custom-I/O buffer.");
		close();
		return false;
	}
	m_io_context = avio_alloc_context(
		io_buffer,
		io_buffer_size,
		0,
		this,
		&FFmpegMp4VolumetricContainer::read_source,
		nullptr,
		&FFmpegMp4VolumetricContainer::seek_source);
	if (m_io_context == nullptr)
	{
		av_free(io_buffer);
		set_error("Could not create the FFmpeg custom-I/O context.");
		close();
		return false;
	}
	m_io_context->seekable =
		m_source->is_seekable() ? AVIO_SEEKABLE_NORMAL : 0;

	m_context = avformat_alloc_context();
	if (m_context == nullptr)
	{
		set_error("Could not allocate the FFmpeg format context.");
		close();
		return false;
	}
	m_context->pb = m_io_context;
	m_context->flags |= AVFMT_FLAG_CUSTOM_IO;

	// Fragmented OpenVolumetric files carry a terminal mfra/tfra index. FFmpeg's
	// MOV demuxer leaves use_mfra_for=auto dormant during its initial root-atom
	// walk and otherwise scans every moof to build the same index. Selecting PTS
	// explicitly makes the first moof trigger the bounded tail-index lookup.
	AVDictionary* input_options = nullptr;
	av_dict_set(&input_options, "use_mfra_for", "pts", 0);
	const int result = avformat_open_input(
		&m_context, nullptr, nullptr, &input_options);
	av_dict_free(&input_options);
	if (result < 0)
	{
		const std::string source_error = m_source->error();
		set_error(
			"Could not open MP4 byte source: " + ffmpeg_error(result) +
			(source_error.empty() ? "" : " (" + source_error + ")"));
		close();
		return false;
	}
	return finish_open();
}

bool FFmpegMp4VolumetricContainer::finish_open()
{
	// OpenVolumetric accepts authored MP4 input, whose moov sample tables and
	// codec parameters are complete after avformat_open_input(). The generic
	// stream-info probe is intended for formats that require packet inspection;
	// with a custom geometry data track it scans every fragment looking for
	// additional codec information. On HTTP that downloaded the complete file
	// before startup or adaptive candidate preparation could finish.
	if (!discover_streams())
	{
		const std::string discovery_error = m_error;
		close();
		m_error = discovery_error;
		return false;
	}
	if (!discover_indexed_duration())
	{
		const std::string duration_error = m_error;
		close();
		m_error = duration_error;
		return false;
	}
	if (auto* remote = dynamic_cast<HttpRangeByteSource*>(m_source.get()))
		remote->enable_fragment_prefetch();
	m_end_of_stream = false;
	m_error.clear();
	return true;
}

void FFmpegMp4VolumetricContainer::close()
{
	if (m_source)
		m_source->cancel();
	if (m_context != nullptr)
		avformat_close_input(&m_context);
	if (m_io_context != nullptr)
		avio_context_free(&m_io_context);
	m_source.reset();
	m_stream_indices = {{-1, -1, -1}};
	m_duration_seconds = 0.0;
	m_end_of_stream = false;
}

bool FFmpegMp4VolumetricContainer::discover_indexed_duration()
{
	const int video_index = m_stream_indices[kind_slot(StreamKind::Video)];
	if (video_index < 0)
		return false;

	// mfra makes an INT64_MAX backward seek land on the final video random
	// access point. Reading to EOF therefore examines one fragment rather than
	// the complete presentation and recovers the duration omitted from the
	// fragmented mvhd written by FFmpeg.
	if (av_seek_frame(
			m_context,
			video_index,
			std::numeric_limits<std::int64_t>::max(),
			AVSEEK_FLAG_BACKWARD) < 0)
	{
		set_error("Could not seek to the final indexed MP4 fragment.");
		return false;
	}
	AVPacket packet{};
	double video_end = 0.0;
	while (av_read_frame(m_context, &packet) >= 0)
	{
		if (packet.stream_index == video_index && packet.pts != AV_NOPTS_VALUE)
		{
			const AVStream* stream = m_context->streams[video_index];
			const std::int64_t end = packet.pts + std::max<std::int64_t>(
				packet.duration, 0);
			video_end = std::max(
				video_end,
				static_cast<double>(end) * av_q2d(stream->time_base));
		}
		av_packet_unref(&packet);
	}
	av_packet_unref(&packet);
	if (!(video_end > 0.0) || !std::isfinite(video_end))
	{
		set_error("Could not derive duration from the final MP4 fragment.");
		return false;
	}
	avformat_close_input(&m_context);
	if (avio_seek(m_io_context, 0, SEEK_SET) < 0)
	{
		set_error("Could not rewind MP4 input after duration discovery.");
		return false;
	}
	m_context = avformat_alloc_context();
	if (m_context == nullptr)
	{
		set_error("Could not recreate the MP4 format context.");
		return false;
	}
	m_context->pb = m_io_context;
	m_context->flags |= AVFMT_FLAG_CUSTOM_IO;
	AVDictionary* input_options = nullptr;
	av_dict_set(&input_options, "use_mfra_for", "pts", 0);
	const int reopen_result = avformat_open_input(
		&m_context, nullptr, nullptr, &input_options);
	av_dict_free(&input_options);
	if (reopen_result < 0)
	{
		set_error("Could not reopen MP4 after duration discovery: " +
			ffmpeg_error(reopen_result));
		return false;
	}
	m_stream_indices = {{-1, -1, -1}};
	if (!discover_streams())
		return false;
	m_duration_seconds = video_end;
	m_end_of_stream = false;
	return true;
}

int FFmpegMp4VolumetricContainer::read_source(
	void* opaque,
	std::uint8_t* buffer,
	int size)
{
	auto* container =
		static_cast<FFmpegMp4VolumetricContainer*>(opaque);
	if (container == nullptr || !container->m_source ||
		buffer == nullptr || size <= 0)
	{
		return AVERROR(EINVAL);
	}
	const std::int64_t result = container->m_source->read(
		buffer,
		static_cast<std::size_t>(size));
	if (result > 0)
		return static_cast<int>(result);
	if (result == 0)
		return AVERROR_EOF;
	return container->m_source->is_cancelled()
		? AVERROR_EXIT
		: AVERROR(EIO);
}

std::int64_t FFmpegMp4VolumetricContainer::seek_source(
	void* opaque,
	std::int64_t offset,
	int whence)
{
	auto* container =
		static_cast<FFmpegMp4VolumetricContainer*>(opaque);
	if (container == nullptr || !container->m_source)
		return AVERROR(EINVAL);
	if (whence == AVSEEK_SIZE)
		return container->m_source->size();
	if (!container->m_source->is_seekable())
		return AVERROR(ENOSYS);

	const int origin = whence & ~AVSEEK_FORCE;
	ByteSeekOrigin seek_origin;
	switch (origin)
	{
	case SEEK_SET:
		seek_origin = ByteSeekOrigin::Begin;
		break;
	case SEEK_CUR:
		seek_origin = ByteSeekOrigin::Current;
		break;
	case SEEK_END:
		seek_origin = ByteSeekOrigin::End;
		break;
	default:
		return AVERROR(EINVAL);
	}
	const std::int64_t result =
		container->m_source->seek(offset, seek_origin);
	if (result >= 0)
		return result;
	return container->m_source->is_cancelled()
		? AVERROR_EXIT
		: AVERROR(EIO);
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
	// Anchor seeks to the video track. With the global (-1) timeline FFmpeg may
	// select the continuously keyed audio track in a fragmented file and fall
	// back to its first sample, forcing adaptive preparation to decode from
	// timestamp zero. Authoring aligns video and independent geometry access
	// points, so the preceding video keyframe is the coupled random-access point.
	const int video_index = m_stream_indices[kind_slot(StreamKind::Video)];
	if (video_index < 0)
	{
		set_error("Seek requires a discovered video stream.");
		return false;
	}
	const std::int64_t media_timestamp = static_cast<std::int64_t>(
		std::llround(seconds * static_cast<double>(AV_TIME_BASE)));
	const std::int64_t timestamp = av_rescale_q(
		media_timestamp,
		AV_TIME_BASE_Q,
		m_context->streams[video_index]->time_base);
	const int result = av_seek_frame(
		m_context, video_index, timestamp, AVSEEK_FLAG_BACKWARD);
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

void FFmpegMp4VolumetricContainer::cancel_pending_io()
{
	if (m_source)
		m_source->cancel();
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

ByteSourceDiagnostics
FFmpegMp4VolumetricContainer::source_diagnostics() const
{
	return m_source != nullptr
		? m_source->diagnostics()
		: ByteSourceDiagnostics{};
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
	if (m_duration_seconds > 0.0)
		return m_duration_seconds;
	return m_context == nullptr || m_context->duration <= 0
		? 0.0
		: static_cast<double>(m_context->duration) / AV_TIME_BASE;
}

void FFmpegMp4VolumetricContainer::set_error(const std::string& message)
{
	m_error = message;
	m_end_of_stream = false;
}

} // namespace openvolumetric
