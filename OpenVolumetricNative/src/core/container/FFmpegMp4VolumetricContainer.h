#pragma once

#include "IVolumetricContainer.h"

extern "C"
{
#include <libavcodec/codec_par.h>
#include <libavformat/avformat.h>
}

#include <array>
#include <memory>
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
	/// Opens through FFmpeg custom I/O and assumes ownership of source.
	bool open(std::unique_ptr<IByteSource> source) override;
	/// Releases demux state and invalidates native stream pointers.
	void close() override;
	/// Copies the next recognized AVPacket into an owning ContainerPacket.
	bool read(ContainerPacket& packet) override;
	/// Seeks the shared demuxer and clears its end-of-stream condition.
	bool seek(double seconds) override;
	/// Cancels custom source I/O; local FFmpeg input has nothing to cancel.
	void cancel_pending_io() override;

	/// Returns whether m_context currently owns an open input.
	bool is_open() const override;
	/// Returns whether the previous read reached AVERROR_EOF.
	bool end_of_stream() const override;
	/// Returns the last human-readable FFmpeg or validation error.
	const std::string& error() const override;
	ByteSourceDiagnostics source_diagnostics() const override;

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
	/// Completes FFmpeg stream discovery after native or custom I/O opens.
	bool finish_open();
	/// FFmpeg read callback; invoked only by the demux owner thread.
	static int read_source(void* opaque, std::uint8_t* buffer, int size);
	/// FFmpeg seek/size callback; invoked only by the demux owner thread.
	static std::int64_t seek_source(
		void* opaque,
		std::int64_t offset,
		int whence);
	/// Stores message and writes it to the native diagnostic logger.
	void set_error(const std::string& message);

	AVFormatContext* m_context;
	AVIOContext* m_io_context;
	std::unique_ptr<IByteSource> m_source;
	std::array<int, 3> m_stream_indices;
	bool m_end_of_stream;
	std::string m_error;
};

} // namespace openvolumetric
