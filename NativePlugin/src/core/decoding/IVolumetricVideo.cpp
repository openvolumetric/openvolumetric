#include "IVolumetricVideo.h"

#include <utility>

bool IVolumetricVideo::submit_embedded_geometry(int frame_index)
{
	if (m_avdecoder == nullptr || m_geometrydecoder == nullptr ||
		!m_avdecoder->has_embedded_geometry())
	{
		return true;
	}

	// Geometry decoding happens asynchronously. Feed Draco ahead of the frame
	// Unity is currently requesting so a decoded mesh is ready when its video
	// frame is presented. This also drains any late packets instead of silently
	// dropping them when Unity advances to the next frame.
	constexpr int geometry_lookahead_frames = 120;
	const int submission_limit = frame_index + geometry_lookahead_frames;

	IAVDecoder::EncodedGeometryFrame encoded;
	while (m_avdecoder->get_geometry_data(submission_limit, encoded))
	{
		if (!m_geometrydecoder->submit_encoded_frame(
			encoded.frame_index,
			std::move(encoded.payload)))
		{
			return false;
		}
	}

	return true;
}
