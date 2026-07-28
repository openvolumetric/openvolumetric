#pragma once

#include "IVolumetricContainer.h"

extern "C"
{
#include <libavcodec/codec_par.h>
#include <libavformat/avformat.h>
}

#include <array>
#include <string>

namespace openvolumetric
{

/// MP4 container implementation. This is the sole owner of AVFormatContext
/// and the sole component that calls av_read_frame() and av_seek_frame().
class FFmpegMp4VolumetricContainer final : public IVolumetricContainer
{
public:
	/// Constructs a closed container with no discovered streams.
	FFmpegMp4VolumetricContainer();
	/// Closes the underlying AVFormatContext.
	~FFmpegMp4VolumetricContainer() override;

	/// Opens an MP4 and discovers exactly one video and geometry stream, plus
	/// an optional audio stream.
	bool open(const char* path) override;
	/// Releases demux state and invalidates native stream pointers.
	void close() override;
	/// Copies the next recognized AVPacket into an owning ContainerPacket.
	bool read(ContainerPacket& packet) override;
	/// Seeks the shared demuxer and clears its end-of-stream condition.
	bool seek(double seconds) override;

	/// Returns whether m_context currently owns an open input.
	bool is_open() const override;
	/// Returns whether the previous read reached AVERROR_EOF.
	bool end_of_stream() const override;
	/// Returns the last human-readable FFmpeg or validation error.
	const std::string& error() const override;

	/// Returns the discovered FFmpeg stream index, or -1 when absent.
	int stream_index(StreamKind kind) const;
	/// Returns a borrowed AVStream pointer owned by m_context.
	AVStream* native_stream(StreamKind kind) const;
	/// Returns borrowed codec parameters for a discovered stream.
	const AVCodecParameters* codec_parameters(StreamKind kind) const;
	/// Returns the container duration converted to seconds.
	double duration_seconds() const;

private:
	/// Classifies streams and enforces the OpenVolumetric MP4 track requirements.
	bool discover_streams();
	/// Stores message and writes it to the native diagnostic logger.
	void set_error(const std::string& message);

	AVFormatContext* m_context;
	std::array<int, 3> m_stream_indices;
	bool m_end_of_stream;
	std::string m_error;
};

} // namespace openvolumetric
