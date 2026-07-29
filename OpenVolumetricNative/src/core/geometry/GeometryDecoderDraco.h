#pragma once

#include <BoundedQueue.h>
#include <IGeometryDecoder.h>

#include<Mesh.h>

#include<atomic>
#include<thread>

namespace openvolumetric
{

/// Draco worker for geometry samples extracted from the MP4 vvge track.
///
/// submit_encoded_frame() is called from the render-side coordinator.
/// m_decode_thread consumes compressed frames and publishes engine-neutral
/// Mesh objects for matching by presentation timestamp.
class GeometryDecoderDraco : public IGeometryDecoder
{


public:
	/// Byte representation accepted by the Draco decoder API.
	typedef std::vector<char> DracoData;
	
	/// Constructs empty bounded input/output queues and a stopped worker.
	GeometryDecoderDraco();

	/// Stops the worker and releases queued mesh data.
	~GeometryDecoderDraco() override;

	/// Resets queues and prepares the worker for embedded geometry samples.
	bool init() override;

	/// Copies a compressed sample into the worker input queue.
	bool submit_encoded_frame(
		std::uint64_t generation,
		double presentation_time,
		GeometryPacket packet) override;

	/// Reports input capacity without consuming a media-side sample.
	bool can_accept_encoded_frame() const override;

	/// Returns a persistent input, decode, or output queue error.
	std::string get_last_error() const override;

	/// Drops stale work and makes generation the only accepted timeline pass.
	void reset(std::uint64_t generation) override;

	/// Propagates input EOS once every sample for generation was submitted.
	void mark_end_of_stream(std::uint64_t generation) override;

	/// Starts the single Draco worker thread.
	bool start_decoding() override;

	/// Stops and joins the Draco worker thread.
	bool stop_decoding() override;

	/// Selects and removes the mesh nearest presentation_time within tolerance.
	openvolumetric::FrameMatchResult get_mesh_data(
		double presentation_time,
		double tolerance,
		double& actual_presentation_time,
		Mesh& mesh) override;
	

	/// Compatibility no-op: selected meshes transfer ownership by value.
	void clear_frame_data() override;

	/// Stops decoding and clears both bounded queues.
	void destroy() override;

protected:
	/// Worker loop that converts queued Draco payloads into timed Mesh objects.
	bool decode();

	/// Returns whether output backpressure should temporarily pause decoding.
	bool is_buffer_blocked();

	/// Decodes one independently decodable Draco payload into mesh_out.
	bool convert_draco_to_mesh(DracoData & data, Mesh& mesh_out);

	/// Applies one validated position update to the active topology.
	bool convert_draco_update_to_mesh(
		const GeometryPacket& packet,
		Mesh& mesh_out);

	/// Clears compressed and decoded queues while preserving current generation.
	void flush_buffer();

private:

	struct EncodedMeshData
	{
		/// Seek/loop pass that owns this work item.
		std::uint64_t generation = 0;
		GeometryPacket packet;
		double presentation_time = 0.0;
	};

	openvolumetric::BoundedQueue<EncodedMeshData> m_streamed_meshes;

	/// Decoded mesh and the MP4 timestamp/generation that identify it.
	struct MeshData
	{
		std::uint64_t generation = 0;
		Mesh mesh;

		double presentation_time = 0.0;
	};

	/// Bounded output queue consumed by the render-side coordinator.
	openvolumetric::BoundedQueue<MeshData> m_decoded_meshes;
	std::atomic<std::uint64_t> m_generation;
	std::atomic<std::uint64_t> m_end_of_stream_generation;
	std::atomic<bool> m_decode_active;

	/// Active topology cached exclusively on the Draco worker thread.
	Mesh m_topology_mesh;
	std::uint64_t m_topology_id = 0;
	std::uint32_t m_topology_keyframe = 0;
	std::uint64_t m_topology_generation = 0;


	/// Single worker that exclusively invokes the Draco decoder.
	std::thread m_decode_thread;

};

} // namespace openvolumetric
