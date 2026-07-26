#pragma once

#include <IDecoder.h>

#include <Mesh.h>

#include <cstdint>
#include <vector>

/// Asynchronous decoder for compressed geometry samples.
///
/// The media decoder submits Draco payloads with their presentation index.
/// A worker converts them to Mesh objects, which the render integration later
/// retrieves using the same index as the corresponding video frame.
class IGeometryDecoder : public IDecoder
{

public:

	// --------------------------------------------------------------------------
	// Constructor
	// --------------------------------------------------------------------------
	IGeometryDecoder() = default;


	// --------------------------------------------------------------------------
	// destructor
	// --------------------------------------------------------------------------
	~IGeometryDecoder() override {};

	/// Clears previous state and prepares the embedded-geometry pipeline.
	virtual bool init() = 0;

	/// Transfers one compressed frame into the decoder's input queue.
	virtual bool submit_encoded_frame(
		std::uint64_t generation,
		int frame_index,
		std::vector<std::uint8_t> payload) = 0;

	/// Discards work belonging to an earlier seek or loop pass.
	virtual void reset(std::uint64_t generation) = 0;

	/// Retrieves an already-decoded mesh for exactly frame_index.
	virtual bool get_mesh_data(int frame_index, Mesh& mesh) = 0;

	/// Discards the most recently consumed decoded mesh.
	virtual void clear_frame_data() = 0;
	

	virtual void destroy() = 0;

};
