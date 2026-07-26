#pragma once

#include "IVolumetricContainer.h"

extern "C"
{
#include <libavcodec/codec_par.h>
#include <libavformat/avformat.h>
}

#include <array>
#include <string>

namespace volumetric_video
{

/// MP4 container implementation. This is the sole owner of AVFormatContext
/// and the sole component that calls av_read_frame() and av_seek_frame().
class FFmpegMp4VolumetricContainer final : public IVolumetricContainer
{
public:
	FFmpegMp4VolumetricContainer();
	~FFmpegMp4VolumetricContainer() override;

	bool open(const char* path) override;
	void close() override;
	bool read(ContainerPacket& packet) override;
	bool seek(double seconds) override;

	bool is_open() const override;
	bool end_of_stream() const override;
	const std::string& error() const override;

	int stream_index(StreamKind kind) const;
	AVStream* native_stream(StreamKind kind) const;
	const AVCodecParameters* codec_parameters(StreamKind kind) const;
	double duration_seconds() const;

private:
	bool discover_streams();
	void set_error(const std::string& message);

	AVFormatContext* m_context;
	std::array<int, 3> m_stream_indices;
	bool m_end_of_stream;
	std::string m_error;
};

} // namespace volumetric_video
