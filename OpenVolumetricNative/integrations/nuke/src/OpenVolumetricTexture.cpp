#include "DDImage/Format.h"
#include "DDImage/Hash.h"
#include "DDImage/Iop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Row.h"

#include "NukeReaderSession.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

namespace openvolumetric::nuke
{
namespace
{
constexpr const char* kNodeClass = "OpenVolumetricTexture";
constexpr const char* kNodeHelp =
	"Reads the synchronized texture track from one OpenVolumetric MP4. "
	"This native node is paired with OpenVolumetricGeo by the artist-facing "
	"OpenVolumetricRead toolset.";

float clamp_unit(float value)
{
	return std::max(0.0F, std::min(1.0F, value));
}

class OpenVolumetricTexture final : public DD::Image::Iop
{
public:
	explicit OpenVolumetricTexture(Node* node)
		: Iop(node)
	{
		inputs(0);
	}

	const char* Class() const override
	{
		return kNodeClass;
	}

	const char* node_help() const override
	{
		return kNodeHelp;
	}

	void knobs(DD::Image::Knob_Callback callback) override
	{
		DD::Image::File_knob(
			callback, &m_file, "file", "OpenVolumetric MP4");
		DD::Image::Int_knob(
			callback, &m_start_frame, "start_frame", "start frame");
		DD::Image::Double_knob(
			callback, &m_frame_offset, "frame_offset", "frame offset");
		DD::Image::Double_knob(
			callback,
			&m_source_frame_rate,
			"source_frame_rate",
			"source frame rate");
		DD::Image::Tooltip(
			callback, "Use 0 to read the native frame rate from the MP4.");
		DD::Image::Double_knob(callback, &m_trim_out, "trim_out", "trim out");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
		DD::Image::Int_knob(callback, &m_before_mode, "before_mode", "before mode");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
		DD::Image::Int_knob(callback, &m_after_mode, "after_mode", "after mode");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
		DD::Image::Double_knob(
			callback, &m_media_frame_rate, "media_frame_rate", "media frame rate");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN | DD::Image::Knob::DISABLED);
		DD::Image::Double_knob(
			callback, &m_media_duration, "media_duration", "media duration");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN | DD::Image::Knob::DISABLED);
		DD::Image::Divider(callback);
		DD::Image::Float_knob(
			callback,
			&m_luminance_correction,
			"luminance_correction",
			"luminance correction");
		DD::Image::Float_knob(
			callback,
			&m_red_projection_correction,
			"red_projection_correction",
			"red projection correction");
		DD::Image::Float_knob(
			callback,
			&m_blue_projection_correction,
			"blue_projection_correction",
			"blue projection correction");
		DD::Image::Int_knob(
			callback, &m_cache_frames, "cache_frames", "cache frames");
		DD::Image::Int_knob(
			callback, &m_cache_megabytes, "cache_megabytes", "cache MB");
		DD::Image::Int_knob(
			callback, &m_reload_version, "reload_version", "reload version");
		DD::Image::SetFlags(callback, DD::Image::Knob::HIDDEN);
	}

	void append(DD::Image::Hash& hash) override
	{
		Iop::append(hash);
		hash.append(m_file ? m_file : "");
		hash.append(m_start_frame);
		hash.append(m_frame_offset);
		hash.append(m_source_frame_rate);
		hash.append(m_trim_out);
		hash.append(m_before_mode);
		hash.append(m_after_mode);
		hash.append(m_luminance_correction);
		hash.append(m_red_projection_correction);
		hash.append(m_blue_projection_correction);
		hash.append(m_cache_frames);
		hash.append(m_cache_megabytes);
		hash.append(m_reload_version);
		hash.append(outputContext().frame());
	}

	void _validate(bool) override
	{
		const std::string source = m_file ? m_file : "";
		std::string session_error;
		auto session = NukeReaderSession::acquire(
			source,
			static_cast<std::size_t>(std::max(0, m_cache_frames)),
			static_cast<std::size_t>(std::max(0, m_cache_megabytes)),
			m_reload_version,
			session_error);
		if (!session)
		{
			error("OpenVolumetric: %s", session_error.c_str());
			return;
		}

		const OpenVolumetricMediaInfo media = session->media_info();
		m_media_frame_rate = media.frame_rate;
		m_media_duration = media.duration;
		// These are output metadata, not artist-authored values. Directly changing
		// the backing members during validation does not update the Python-visible
		// Knob objects, so publish through Nuke's value API as well.
		if (DD::Image::Knob* frame_rate_knob = knob("media_frame_rate"))
			frame_rate_knob->set_value(m_media_frame_rate);
		if (DD::Image::Knob* duration_knob = knob("media_duration"))
			duration_knob->set_value(m_media_duration);
		const double source_rate = m_source_frame_rate > 0.0
			? m_source_frame_rate
			: media.frame_rate;
		if (!std::isfinite(source_rate) || source_rate <= 0.0)
		{
			error("OpenVolumetric: source frame rate must be positive.");
			return;
		}

		const double final_source_frame = std::max(
			0.0, std::ceil(media.duration * source_rate) - 1.0);
		const double trim_in = std::clamp(
			m_frame_offset, 0.0, final_source_frame);
		const double trim_out = m_trim_out >= 0.0
			? std::clamp(m_trim_out, trim_in, final_source_frame)
			: final_source_frame;
		const double requested_source_frame =
			outputContext().frame() - static_cast<double>(m_start_frame) +
			m_frame_offset;
		const bool outside_none =
			(requested_source_frame < trim_in && m_before_mode == 0) ||
			(requested_source_frame > trim_out && m_after_mode == 0);
		if (outside_none)
		{
			std::lock_guard<std::mutex> lock(m_presentation_mutex);
			m_session = std::move(session);
			m_presentation.reset();
			configure_info(media, source_rate);
			return;
		}
		const double presentation_time =
			std::clamp(requested_source_frame, trim_in, trim_out) / source_rate;
		std::shared_ptr<const OpenVolumetricPresentation> presentation;
		const FrameRequestResult result =
			session->request(presentation_time, presentation);
		if (result != FrameRequestResult::Ready || !presentation)
		{
			const std::string detail = session->error();
			error(
				"OpenVolumetric: %s%s%s",
				request_result_message(result),
				detail.empty() ? "" : " ",
				detail.c_str());
			return;
		}

		if (!valid_yuv420(*presentation))
		{
			error("OpenVolumetric: decoded texture planes have invalid sizes.");
			return;
		}

		{
			// Publish only complete immutable presentations. Retaining the
			// previous frame during decode prevents Nuke's concurrent material
			// evaluation from observing an empty texture and flashing grey.
			std::lock_guard<std::mutex> lock(m_presentation_mutex);
			m_session = std::move(session);
			m_presentation = std::move(presentation);
		}

		// Info2D retains pointers to Format objects; this storage must live for
		// at least as long as the node rather than being local to _validate().
		configure_info(media, source_rate);
	}

	void engine(
		int y,
		int x,
		int right,
		DD::Image::ChannelMask channels,
		DD::Image::Row& row) override
	{
		std::shared_ptr<const OpenVolumetricPresentation> presentation;
		{
			std::lock_guard<std::mutex> lock(m_presentation_mutex);
			presentation = m_presentation;
		}
		if (!presentation || y < 0 || y >= presentation->height)
			return;

		const int first = std::max(0, x);
		const int last = std::min(presentation->width, right);
		if (first >= last)
			return;

		float* red = channels.contains(DD::Image::Chan_Red)
			? row.writable(DD::Image::Chan_Red)
			: nullptr;
		float* green = channels.contains(DD::Image::Chan_Green)
			? row.writable(DD::Image::Chan_Green)
			: nullptr;
		float* blue = channels.contains(DD::Image::Chan_Blue)
			? row.writable(DD::Image::Chan_Blue)
			: nullptr;
		float* alpha = channels.contains(DD::Image::Chan_Alpha)
			? row.writable(DD::Image::Chan_Alpha)
			: nullptr;

		// FFmpeg publishes top-down planes; Nuke scanlines use a bottom-up
		// image coordinate, so invert only the row addressing here.
		const int source_y = presentation->height - 1 - y;
		const int chroma_width = (presentation->width + 1) / 2;
		for (int column = first; column < last; ++column)
		{
			const std::size_t pixel = static_cast<std::size_t>(
				source_y * presentation->width + column);
			const std::size_t chroma = static_cast<std::size_t>(
				(source_y / 2) * chroma_width + column / 2);
			const float luma =
				static_cast<float>(presentation->y[pixel]) / 255.0F +
				m_luminance_correction;
			const float u =
				static_cast<float>(presentation->u[chroma]) / 255.0F *
					0.872F -
				0.436F + m_blue_projection_correction;
			const float v =
				static_cast<float>(presentation->v[chroma]) / 255.0F *
					1.230F -
				0.615F + m_red_projection_correction;
			if (red)
				red[column] = clamp_unit(luma + 1.13983F * v);
			if (green)
				green[column] = clamp_unit(
					luma - 0.39465F * u - 0.58060F * v);
			if (blue)
				blue[column] = clamp_unit(luma + 2.03211F * u);
			if (alpha)
				alpha[column] = 1.0F;
		}
	}

	static DD::Image::Op* build(Node* node)
	{
		return new OpenVolumetricTexture(node);
	}

private:
	void configure_info(const OpenVolumetricMediaInfo& media, double source_rate)
	{
		m_format = DD::Image::Format(media.width, media.height, 1.0);
		info_.format(m_format);
		info_.full_size_format(m_format);
		info_.set(0, 0, media.width, media.height);
		info_.channels(DD::Image::Mask_RGBA);
		info_.black_outside(true);
		info_.first_frame(m_start_frame);
		const double final_source_frame = std::max(
			0.0, std::ceil(media.duration * source_rate) - 1.0);
		const double trim_out = m_trim_out >= 0.0
			? std::min(m_trim_out, final_source_frame)
			: final_source_frame;
		const int frame_count = std::max(1, static_cast<int>(
			std::floor(std::max(0.0, trim_out - m_frame_offset))) + 1);
		info_.last_frame(m_start_frame + frame_count - 1);
	}

	static bool valid_yuv420(const OpenVolumetricPresentation& presentation)
	{
		if (presentation.width <= 0 || presentation.height <= 0)
			return false;
		const std::size_t luma_size = static_cast<std::size_t>(
			presentation.width * presentation.height);
		const std::size_t chroma_size = static_cast<std::size_t>(
			((presentation.width + 1) / 2) *
			((presentation.height + 1) / 2));
		return presentation.y.size() >= luma_size &&
			presentation.u.size() >= chroma_size &&
			presentation.v.size() >= chroma_size;
	}

	const char* m_file = nullptr;
	int m_start_frame = 1;
	double m_frame_offset = 0.0;
	double m_source_frame_rate = 0.0;
	double m_trim_out = -1.0;
	int m_before_mode = 1;
	int m_after_mode = 1;
	double m_media_frame_rate = 0.0;
	double m_media_duration = 0.0;
	float m_luminance_correction = 0.0F;
	float m_red_projection_correction = 0.0F;
	float m_blue_projection_correction = 0.0F;
	int m_cache_frames = 8;
	int m_cache_megabytes = 256;
	int m_reload_version = 0;
	std::mutex m_presentation_mutex;
	std::shared_ptr<NukeReaderSession> m_session;
	std::shared_ptr<const OpenVolumetricPresentation> m_presentation;
	DD::Image::Format m_format{1, 1, 1.0};

	static const DD::Image::Iop::Description description_;
};

const DD::Image::Iop::Description OpenVolumetricTexture::description_(
	kNodeClass,
	OpenVolumetricTexture::build);
} // namespace
} // namespace openvolumetric::nuke
