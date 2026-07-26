#include "IVolumetricVideo.h"

#include <utility>

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
	while (m_avdecoder->get_geometry_data(submission_limit, encoded))
	{
		if (!m_geometrydecoder->submit_encoded_frame(
			encoded.generation,
			encoded.frame_index,
			encoded.presentation_time,
			std::move(encoded.payload)))
		{
			return false;
		}
	}
	if (m_avdecoder->geometry_end_of_stream())
		m_geometrydecoder->mark_end_of_stream(generation);

	return true;
}
