#include "GeometryDecoderDraco.h"


#include <Logger.h>

#include "draco/compression/decode.h"
#include "draco/core/cycle_timer.h"

#include <chrono>
#include <limits>

// --------------------------------------------------------------------------
// Constructor
// --------------------------------------------------------------------------
GeometryDecoderDraco::GeometryDecoderDraco():
	IGeometryDecoder(), m_streamed_meshes(256), m_decoded_meshes(64),
	m_generation(0),
	m_end_of_stream_generation(
		std::numeric_limits<std::uint64_t>::max()),
	m_decode_active(false)
{

}

// --------------------------------------------------------------------------
// Destructor
// --------------------------------------------------------------------------
GeometryDecoderDraco::~GeometryDecoderDraco()
{
}

// --------------------------------------------------------------------------
// Destroy
// --------------------------------------------------------------------------
void GeometryDecoderDraco::destroy()
{
	LOG("GeometryDecoderDraco::destroy - start");

	//
	flush_buffer();

	m_streamed_meshes.clear();

	//
	LOG("GeometryDecoderDraco::destroy - stop");
}


bool GeometryDecoderDraco::init()
{
	flush_buffer();
	m_streamed_meshes.clear();
	m_initialised = true;
	m_decoder_state = INITIALIZED;
	LOG("GeometryDecoderDraco::init - embedded geometry enabled");
	return true;
}

bool GeometryDecoderDraco::submit_encoded_frame(
	std::uint64_t generation,
	int frame_index,
	double presentation_time,
	std::vector<std::uint8_t> payload)
{
	if (!m_initialised || payload.empty())
		return false;

	EncodedMeshData encoded;
	encoded.generation = generation;
	encoded.frame_index = frame_index;
	encoded.presentation_time = presentation_time;
	encoded.data.assign(payload.begin(), payload.end());
	if (!m_streamed_meshes.try_push(std::move(encoded)))
	{
		m_streamed_meshes.set_error("Compressed geometry queue is full.");
		return false;
	}
	return true;
}

void GeometryDecoderDraco::reset(std::uint64_t generation)
{
	m_generation.store(generation, std::memory_order_release);
	m_end_of_stream_generation.store(
		std::numeric_limits<std::uint64_t>::max(),
		std::memory_order_release);
	m_streamed_meshes.clear();
	m_decoded_meshes.clear();
}

void GeometryDecoderDraco::mark_end_of_stream(std::uint64_t generation)
{
	m_end_of_stream_generation.store(generation, std::memory_order_release);
}
 
// --------------------------------------------------------------------------
// Start Decoding
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::start_decoding()
{
	LOG("GeometryDecoderDraco::start_decoding");

	//
	if (!this->m_initialised)
	{
		LOG("GeometryDecoderDraco::start_decoding - not INITIALIZED");
		return false;
	}

	// Create thread start video decoding
	m_decode_thread = std::thread([&]()
	{
		// 
		m_decoder_state = DECODING;

		//
		while (m_decoder_state != STOP)
		{
			// Switch based on decoder state
			switch (m_decoder_state)
			{
				// If decoding
				case DECODING:
				{
					if (!this->decode())
					{
						m_decoder_state = DECODE_EOF;
					}
					break;
				}
				case SEEK:
				{
					LOG("GeometryDecoderDraco::start_decoding - seek");

					break;
				}
				case DECODE_EOF:
				{
					LOG("GeometryDecoderDraco::start_decoding - eof");
					m_decoder_state = DECODING;
					break;
				}
			}
		}

		//
		LOG("AVDecoderFFMPEG::start_decoding - end");
	});

	//
	return true;
}



// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::decode()
{
	//
	if (!is_buffer_blocked())
	{
		EncodedMeshData encoded;
		bool has_encoded_mesh = false;
		has_encoded_mesh = m_streamed_meshes.access([&](auto& frames)
		{
			if (frames.empty())
				return false;
			encoded = std::move(frames.front());
			frames.pop_front();
			return true;
		});
		if (!has_encoded_mesh)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			return true;
		}

		m_decode_active.store(true, std::memory_order_release);
		Mesh mesh;
		if (!convert_draco_to_mesh(encoded.data, mesh))
		{
			m_decode_active.store(false, std::memory_order_release);
			m_decoded_meshes.set_error(
				"Draco geometry frame could not be decoded.");
			return false;
		}

		MeshData mesh_data;
		mesh_data.mesh = std::move(mesh);
		mesh_data.generation = encoded.generation;
		mesh_data.frame_index = encoded.frame_index;
		mesh_data.presentation_time = encoded.presentation_time;
		if (encoded.generation !=
			m_generation.load(std::memory_order_acquire))
		{
			m_decode_active.store(false, std::memory_order_release);
			return true;
		}
		if (!m_decoded_meshes.try_push(std::move(mesh_data)))
		{
			m_decode_active.store(false, std::memory_order_release);
			m_decoded_meshes.set_error("Decoded geometry queue is full.");
			return false;
		}
		m_decode_active.store(false, std::memory_order_release);
	}

	return true;
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::is_buffer_blocked()
{
	return m_decoded_meshes.full();
}


// --------------------------------------------------------------------------
// get_mesh_data
// --------------------------------------------------------------------------
volumetric_video::FrameMatchResult GeometryDecoderDraco::get_mesh_data(
	double presentation_time,
	double tolerance,
	double& actual_presentation_time,
	Mesh& mesh)
{
	return m_decoded_meshes.access([&](auto& meshes)
	{
		while (!meshes.empty())
		{
			if (meshes.front().presentation_time <
				presentation_time - tolerance)
			{
				LOG(
					"SYNC dropped geometry sample pts=%f target=%f",
					meshes.front().presentation_time,
					presentation_time);
				meshes.pop_front();
				continue;
			}
			break;
		}
		if (meshes.empty())
		{
			const std::uint64_t generation =
				m_generation.load(std::memory_order_acquire);
			if (m_end_of_stream_generation.load(
					std::memory_order_acquire) == generation &&
				m_streamed_meshes.size() == 0 &&
				!m_decode_active.load(std::memory_order_acquire))
			{
				LOG(
					"SYNC missing final geometry for video pts=%f",
					presentation_time);
				return volumetric_video::FrameMatchResult::Missing;
			}
			return volumetric_video::FrameMatchResult::NotReady;
		}
		if (meshes.front().presentation_time >
			presentation_time + tolerance)
		{
			LOG(
				"SYNC missing geometry for video pts=%f next_geometry=%f",
				presentation_time,
				meshes.front().presentation_time);
			return volumetric_video::FrameMatchResult::Missing;
		}
		mesh = meshes.front().mesh;
		actual_presentation_time = meshes.front().presentation_time;
		return volumetric_video::FrameMatchResult::Ready;
	});
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::convert_draco_to_mesh(DracoData& draco_data, Mesh& mesh_out)
{
	// Init Buffer using data
	draco::DecoderBuffer buffer;
	buffer.Init(draco_data.data(), draco_data.size());
	
	// Create draco::Mesh
	draco::Mesh* mesh = nullptr;
	auto type_statusor = draco::Decoder::GetEncodedGeometryType(&buffer);
	if (!type_statusor.ok()) {
		return false;
	}

	// Check that the mesh type is a triangular mesh
	const draco::EncodedGeometryType geom_type = type_statusor.value();
	if (geom_type != draco::TRIANGULAR_MESH)
	{
		return false;
	}

	// Start timer
	draco::CycleTimer timer;
	timer.Start();

	// Create draco decoder
	draco::Decoder decoder;

	// Decoder Mesh from Buffer
	auto statusor = decoder.DecodeMeshFromBuffer(&buffer);
	if (!statusor.ok())
	{
		return false;
	}

	//
	std::unique_ptr<draco::Mesh> in_mesh = std::move(statusor).value();
	timer.Stop();
	if (in_mesh)
	{
		mesh = in_mesh.get();
	}

	// Allocate space for indexes
	mesh_out.indexes.resize(mesh->num_faces() * 3);
	for (draco::FaceIndex face_id(0); face_id < mesh->num_faces(); ++face_id)
	{
		//
		const draco::Mesh::Face face = mesh->face(face_id);
		// Copy memory contain indices 
		memcpy(&mesh_out.indexes[0] + face_id.value() * 3,
			reinterpret_cast<const int*>(face.data()),
			sizeof(int) * 3);
	}

	// Resize verts array
	mesh_out.verts.resize(mesh->num_points());

	// Get attributes
	const auto pos_att		= mesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
	const auto normal_att	= mesh->GetNamedAttribute(draco::GeometryAttribute::NORMAL);
	const auto uv_att		= mesh->GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
	if (pos_att == nullptr || normal_att == nullptr || uv_att == nullptr)
		return false;

	// Populate for each point
	for (draco::PointIndex i(0); i < mesh->num_points(); ++i)
	{
		// Get Vertex posision
		const draco::AttributeValueIndex pos_val_index = pos_att->mapped_index(i);
		if (!pos_att->ConvertValue<float, 3>(pos_val_index, &mesh_out.verts[i.value()].pos[0]))
		{
			return false;
		}

		// Get Vertex Normal
		const draco::AttributeValueIndex norm_val_index = normal_att->mapped_index(i);
		if (!normal_att->ConvertValue<float, 3>(norm_val_index, &mesh_out.verts[i.value()].normal[0]))
		{
			return false;
		}

		// Get Vertex UV
		const draco::AttributeValueIndex uv_val_index = uv_att->mapped_index(i);
		if (!uv_att->ConvertValue<float, 2>(uv_val_index, &mesh_out.verts[i.value()].uv[0]))
		{
			return false;
		}
	}

	//
	return true;
}


// --------------------------------------------------------------------------
// Stop Decoding
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::stop_decoding()
{
	//
	LOG("GeometryDecoderDraco::stop_decoding");

	//
	this->m_decoder_state = STOP;

	//
	if (m_decode_thread.joinable())
	{
		m_decode_thread.join();
	}

	//
	return true;
}

// --------------------------------------------------------------------------
// Clear front decoded mesh
// --------------------------------------------------------------------------
void GeometryDecoderDraco::clear_frame_data()
{
	m_decoded_meshes.access([](auto& meshes)
	{
		if (!meshes.empty())
			meshes.pop_front();
	});
}

// --------------------------------------------------------------------------
// Flush Buffers
// --------------------------------------------------------------------------
void GeometryDecoderDraco::flush_buffer()
{
	LOG("GeometryDecoderDraco::flush_buffer - start");

	m_decoded_meshes.clear();
	m_streamed_meshes.clear();

	//
	LOG("GeometryDecoderDraco::flush_buffer - stop");
}
