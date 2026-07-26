#include "GeometryDecoderDraco.h"


#include <Logger.h>

#include "draco/compression/decode.h"
#include "draco/core/cycle_timer.h"
#include "draco/io/file_utils.h"
#include "draco/io/parser_utils.h"

#include <cstdio>
#include <fstream>
#include <iterator>

// --------------------------------------------------------------------------
// Constructor
// --------------------------------------------------------------------------
GeometryDecoderDraco::GeometryDecoderDraco():
	IGeometryDecoder(), m_max_buffer_size(64)
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

	//
	m_encoded_meshes.clear();

	//
	LOG("GeometryDecoderDraco::destroy - stop");
}


// --------------------------------------------------------------------------
//
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::init(char* filepattern, int start_index, int stop_index)
{
	// Allocate Memory
	m_encoded_meshes.clear();
	m_encoded_meshes.resize(stop_index - start_index + 1);

	// Load each mesh and add to vector
	#pragma omp parallel for
	for (int i = start_index; i <= stop_index; i++)
	{
		char filepath[1024]; 
		std::snprintf(filepath, sizeof(filepath), filepattern, i);

		// Read the encoded bytes directly. Draco's FileReaderFactory relies on
		// static registration, whose stdio reader can be discarded when Draco
		// is linked as a static library.
		std::ifstream input(filepath, std::ios::binary);
		if (!input)
		{
			LOG("GeometryDecoderDraco::init - Failed opening the input file - %s.", filepath);
			return false;
		}

		std::vector<char> data(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());

		if (data.empty()) 
		{
			LOG("GeometryDecoderDraco::init - Empty input file - %s.", filepath);
			return false;
		}

		LOG("GeometryDecoderDraco::init - Loaded - %s.", filepath);


		// Set mesh Data
		m_encoded_meshes[i - start_index] = data;
	}
	
	//
	this->m_initialised = true;
	
	// Done 
	return true;
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
		//
		LOG("GeometryDecoderDraco::decode - frame %d", m_current_frame);

		// Decode Mesh
		Mesh mesh;
		if (!convert_draco_to_mesh(m_encoded_meshes[m_current_frame], mesh))
		{
			return false;
		}

		// construct mesh data struct
		MeshData mesh_data;
		mesh_data.mesh			= mesh;
		mesh_data.frame_index	= m_current_frame;
	
		// lock geometry mutex and push
		std::lock_guard<std::mutex> lock(m_geometry_mutex);
		m_decoded_meshes.push(mesh_data);

		// update current frame
		update_current_frame();
	}

	return true;
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::is_buffer_blocked()
{
	std::lock_guard<std::mutex> lock(m_geometry_mutex);
	return m_decoded_meshes.size() >= m_max_buffer_size;
}


// --------------------------------------------------------------------------
// 
// --------------------------------------------------------------------------
void GeometryDecoderDraco::update_current_frame()
{
	// increment index
	m_current_frame++;

	// reset if over the number of encoded meshes.
	if (m_current_frame == m_encoded_meshes.size())
	{
		m_current_frame = 0;
	}
}


// --------------------------------------------------------------------------
// get_mesh_data
// --------------------------------------------------------------------------
bool GeometryDecoderDraco::get_mesh_data(int frame_index, Mesh& mesh)
{
	LOG("GeometryDecoderDraco::get_mesh_data - getting mesh at index  %d.", frame_index);

	std::lock_guard<std::mutex> lock(m_geometry_mutex);
	while (!m_decoded_meshes.empty())
	{
		if (m_decoded_meshes.front().frame_index == frame_index)
		{
			mesh = m_decoded_meshes.front().mesh;
			return true;
		}
		else if (frame_index > m_decoded_meshes.front().frame_index)
		{
			LOG("GeometryDecoderDraco::get_mesh_data - clearing front frame  %d.", m_decoded_meshes.front().frame_index);
			m_decoded_meshes.pop();
		}
		else
		{
			break;
		}
	}

	//
	return false;
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
	std::lock_guard<std::mutex> lock(m_geometry_mutex);
	if (!m_decoded_meshes.empty())
		m_decoded_meshes.pop();
}

// --------------------------------------------------------------------------
// Flush Buffers
// --------------------------------------------------------------------------
void GeometryDecoderDraco::flush_buffer()
{
	LOG("GeometryDecoderDraco::flush_buffer - start");

	std::lock_guard<std::mutex> lock(m_geometry_mutex);
	while (!m_decoded_meshes.empty())
		m_decoded_meshes.pop();

	//
	LOG("GeometryDecoderDraco::flush_buffer - stop");
}
