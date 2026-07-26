#pragma once

#include <IGeometryDecoder.h>

#include<Mesh.h>

#include <mutex>
#include<queue>
#include<thread>

/// Draco worker for geometry samples extracted from the MP4 vvge track.
///
/// submit_encoded_frame() is called from the render-side coordinator.
/// m_decode_thread consumes compressed frames and publishes engine-neutral
/// Mesh objects for matching by presentation index.
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
		int frame_index,
		std::vector<std::uint8_t> payload) override;


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
	bool get_mesh_data(int frame_index, Mesh& mesh) override;
	

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
		// Complete independently decodable Draco bitstream.
		DracoData data;
		// Presentation index inherited from the MP4 sample timestamp.
		int frame_index;
	};

	std::queue<EncodedMeshData> m_streamed_meshes;

	// --------------------------------------------------------------------------
	// Mesh data struct encapsulating the decoded meshs and frame index
	// --------------------------------------------------------------------------
	struct MeshData
	{
		// Decoded Mesh Data
		Mesh mesh;

		// Frame index
		int frame_index;
	};

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	std::queue<MeshData> m_decoded_meshes;


	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	unsigned int m_max_buffer_size;


	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	std::thread m_decode_thread;

	// --------------------------------------------------------------------------
	// 
	// --------------------------------------------------------------------------
	std::mutex m_geometry_mutex;

};
