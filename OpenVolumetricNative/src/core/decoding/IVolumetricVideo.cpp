#include "IVolumetricVideo.h"

#include <Logger.h>

#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace openvolumetric
{

bool IVolumetricVideo::submit_embedded_geometry(double presentation_time)
{
	if (m_avdecoder == nullptr || m_geometrydecoder == nullptr ||
		!m_avdecoder->has_embedded_geometry())
	{
		return true;
	}

	const std::uint64_t generation = m_avdecoder->playback_generation();
	if (generation != m_geometry_generation)
	{
		m_geometrydecoder->reset(generation);
		m_geometry_generation = generation;
	}

	// Geometry decoding happens asynchronously. Feed Draco ahead of the frame
	// Unity is currently requesting so a decoded mesh is ready when its video
	// frame is presented. This also drains any late packets instead of silently
	// dropping them when Unity advances to the next frame.
	constexpr double geometry_lookahead_seconds = 4.0;
	const double submission_limit =
		presentation_time + geometry_lookahead_seconds;

	IAVDecoder::EncodedGeometryFrame encoded;
	while (m_geometrydecoder->can_accept_encoded_frame() &&
		m_avdecoder->get_geometry_data(submission_limit, encoded))
	{
		if (!m_geometrydecoder->submit_encoded_frame(
			encoded.generation,
			encoded.presentation_time,
			std::move(encoded.packet)))
		{
			return false;
		}
	}
	if (m_avdecoder->geometry_end_of_stream())
		m_geometrydecoder->mark_end_of_stream(generation);

	return true;
}

bool IVolumetricVideo::prepare_presentation(double presentation_time)
{
	if (m_avdecoder == nullptr || m_geometrydecoder == nullptr)
		return false;

	const double fps = m_avdecoder->get_video_info().fps;
	const double tolerance = fps > 0.0 ? (0.5 / fps) + 0.0001 : 0.017;
	const auto deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (!submit_embedded_geometry(presentation_time))
			return false;

		std::uint8_t* y = nullptr;
		std::uint8_t* u = nullptr;
		std::uint8_t* v = nullptr;
		double video_time = 0.0;
		const auto video_result = m_avdecoder->get_video_data(
			presentation_time,
			tolerance,
			video_time,
			&y,
			&u,
			&v);
		if (video_result == openvolumetric::FrameMatchResult::Ready)
		{
			Mesh mesh;
			double geometry_time = 0.0;
			const auto geometry_result = m_geometrydecoder->get_mesh_data(
				video_time, tolerance, geometry_time, mesh);
			if (geometry_result ==
				openvolumetric::FrameMatchResult::Ready)
			{
				return true;
			}
			if (geometry_result ==
				openvolumetric::FrameMatchResult::Missing)
			{
				LOG(
					"SYNC seek target has no geometry pts=%f",
					video_time);
				return false;
			}
		}
		else if (video_result == openvolumetric::FrameMatchResult::Missing)
		{
			LOG(
				"SYNC seek target has no video pts=%f",
				presentation_time);
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	LOG("SYNC timed out preparing seek target pts=%f", presentation_time);
	return false;
}

} // namespace openvolumetric
