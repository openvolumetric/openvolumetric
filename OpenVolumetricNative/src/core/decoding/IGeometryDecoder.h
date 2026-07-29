#pragma once

#include <IDecoder.h>

#include <Mesh.h>
#include <TimedFrame.h>
#include <GeometryPacket.h>

#include <cstdint>
#include <string>
#include <vector>

namespace openvolumetric
{

/// Asynchronous decoder for compressed geometry samples.
///
/// The media decoder submits Draco payloads with their presentation timestamp.
/// A worker converts them to Mesh objects, which the render integration later
/// retrieves using the timestamp of the corresponding video frame.
class IGeometryDecoder : public IDecoder
{

public:
	IGeometryDecoder() = default;
	~IGeometryDecoder() override {};

	/// Clears previous state and prepares the embedded-geometry pipeline.
	virtual bool init() = 0;

	/// Transfers one compressed frame into the decoder's input queue.
	virtual bool submit_encoded_frame(
		std::uint64_t generation,
		double presentation_time,
		GeometryPacket packet) = 0;

	/// True when the input queue can accept another compressed frame.
	///
	/// The coordinator checks this before removing a packet from the media
	/// queue, so temporary Draco backpressure cannot discard geometry.
	virtual bool can_accept_encoded_frame() const = 0;

	/// Returns a persistent decoder/queue error, or an empty string.
	virtual std::string get_last_error() const = 0;

	/// Discards work belonging to an earlier seek or loop pass.
	virtual void reset(std::uint64_t generation) = 0;

	/// Signals that no more compressed samples belong to this generation.
	virtual void mark_end_of_stream(std::uint64_t generation) = 0;

	/// Selects a mesh by PTS relative to the chosen video sample.
	virtual openvolumetric::FrameMatchResult get_mesh_data(
		double presentation_time,
		double tolerance,
		double& actual_presentation_time,
		Mesh& mesh) = 0;

	/// Discards the most recently consumed decoded mesh.
	virtual void clear_frame_data() = 0;
	

	virtual void destroy() = 0;

};

} // namespace openvolumetric
