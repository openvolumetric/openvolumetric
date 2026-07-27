#pragma once

#include <BoundedQueue.h>
#include <IGeometryDecoder.h>

#include<Mesh.h>

#include<atomic>
#include<thread>

/// Draco worker for geometry samples extracted from the MP4 vvge track.
///
/// submit_encoded_frame() is called from the render-side coordinator.
/// m_decode_thread consumes compressed frames and publishes engine-neutral
/// Mesh objects for matching by presentation timestamp.
class GeometryDecoderDraco : public IGeometryDecoder
{


public:

	// --------------------------------------------------------------------------
	// Typedef for draco data
	// --------------------------------------------------------------------------
	typedef std::vector<char> DracoData;
	
	   
	// --------------------------------------------------------------------------
	// Constructor
	// --------------------------------------------------------------------------
	GeometryDecoderDraco();


	//----------------------------------
	// Destructor
	// --------------------------------------------------------------------------
	~GeometryDecoderDraco() override;


	bool init() override;

	bool submit_encoded_frame(
		std::uint64_t generation,
		double presentation_time,
		std::vector<std::uint8_t> payload) override;

	bool can_accept_encoded_frame() const override;

	std::string get_last_error() const override;

	void reset(std::uint64_t generation) override;

	void mark_end_of_stream(std::uint64_t generation) override;


  	// --------------------------------------------------------------------------
	// Start Decoding
	// --------------------------------------------------------------------------
	bool start_decoding() override;


	// --------------------------------------------------------------------------
	// Stop Decoding
	// --------------------------------------------------------------------------
	bool stop_decoding() override;


	// --------------------------------------------------------------------------
	// Get mesh data for index
	// --------------------------------------------------------------------------
	volumetric_video::FrameMatchResult get_mesh_data(
		double presentation_time,
		double tolerance,
		double& actual_presentation_time,
		Mesh& mesh) override;
	

	// --------------------------------------------------------------------------
	// Clear frame data
	// --------------------------------------------------------------------------
	void clear_frame_data() override;

	// --------------------------------------------------------------------------
	// clean up resources
	// --------------------------------------------------------------------------
	void destroy() override;

protected:


	// --------------------------------------------------------------------------
	// Decode 
	// --------------------------------------------------------------------------
	bool decode();


	// --------------------------------------------------------------------------
	// check if buffer is blocked yet
	// --------------------------------------------------------------------------
	bool is_buffer_blocked();


	// --------------------------------------------------------------------------
	// Stop Decoding
	// --------------------------------------------------------------------------
	bool convert_draco_to_mesh(DracoData & data, Mesh& mesh_out);

	// --------------------------------------------------------------------------
	// Flush Buffers
	// --------------------------------------------------------------------------
	void flush_buffer();

private:

	struct EncodedMeshData
	{
		std::uint64_t generation = 0;
		// Complete independently decodable Draco bitstream.
		DracoData data;
		double presentation_time = 0.0;
	};

	volumetric_video::BoundedQueue<EncodedMeshData> m_streamed_meshes;

	// --------------------------------------------------------------------------
	// Mesh data and its MP4 presentation timestamp.
	// --------------------------------------------------------------------------
	struct MeshData
	{
		std::uint64_t generation = 0;
		// Decoded Mesh Data
		Mesh mesh;

		double presentation_time = 0.0;
	};

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	volumetric_video::BoundedQueue<MeshData> m_decoded_meshes;
	std::atomic<std::uint64_t> m_generation;
	std::atomic<std::uint64_t> m_end_of_stream_generation;
	std::atomic<bool> m_decode_active;


	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	std::thread m_decode_thread;

};
