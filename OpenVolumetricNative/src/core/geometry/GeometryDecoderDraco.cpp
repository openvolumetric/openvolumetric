#include "GeometryDecoderDraco.h"

#include "DracoPointCloudDecoder.h"

#include <Logger.h>

#include "draco/compression/decode.h"
#include "draco/core/cycle_timer.h"

#include <chrono>
#include <cmath>
#include <limits>

namespace openvolumetric
{
GeometryDecoderDraco::GeometryDecoderDraco():
	IGeometryDecoder(), m_streamed_meshes(256), m_decoded_meshes(64),
	m_generation(0),
	m_end_of_stream_generation(
		std::numeric_limits<std::uint64_t>::max()),
	m_decode_active(false),
	m_topology_id(0),
	m_topology_keyframe(0),
	m_topology_generation(0)
{

}
GeometryDecoderDraco::~GeometryDecoderDraco()
{
}

void GeometryDecoderDraco::destroy()
{
	LOG("GeometryDecoderDraco::destroy - start");

	// Joining first makes this safe during partial startup and repeated close.
	stop_decoding();
	flush_buffer();

	m_streamed_meshes.clear();
	m_initialised = false;
	m_decoder_state = UNINITIALIZED;

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
	double presentation_time,
	GeometryPacket packet)
{
	if (!m_initialised || packet.payload.empty())
		return false;

	EncodedMeshData encoded;
	encoded.generation = generation;
	encoded.presentation_time = presentation_time;
	encoded.packet = std::move(packet);
	if (!m_streamed_meshes.try_push(std::move(encoded)))
	{
		m_streamed_meshes.set_error("Compressed geometry queue is full.");
		return false;
	}
	return true;
}

bool GeometryDecoderDraco::can_accept_encoded_frame() const
{
	return m_streamed_meshes.state() == openvolumetric::QueueState::Open &&
		!m_streamed_meshes.full();
}

std::string GeometryDecoderDraco::get_last_error() const
{
	if (m_streamed_meshes.state() == openvolumetric::QueueState::Error)
		return m_streamed_meshes.error();
	if (m_decoded_meshes.state() == openvolumetric::QueueState::Error)
		return m_decoded_meshes.error();
	return {};
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
bool GeometryDecoderDraco::start_decoding()
{
	LOG("GeometryDecoderDraco::start_decoding");

	if (!this->m_initialised)
	{
		LOG("GeometryDecoderDraco::start_decoding - not INITIALIZED");
		return false;
	}

	// The worker is the sole consumer of compressed geometry packets and the
	// sole producer of decoded meshes.
	m_decode_thread = std::thread([&]()
	{
		m_decoder_state = DECODING;

		while (m_decoder_state != STOP)
		{
			switch (m_decoder_state)
			{
				case DECODING:
				{
					if (!this->decode())
					{
						m_decoder_state = DECODE_EOF;
					}
					break;
				}
				case DECODE_EOF:
				{
					LOG("GeometryDecoderDraco::start_decoding - eof");
					m_decoder_state = DECODING;
					break;
				}
				default:
					std::this_thread::yield();
					break;
			}
		}

		LOG("AVDecoderFFMPEG::start_decoding - end");
	});

	return true;
}
bool GeometryDecoderDraco::decode()
{
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
		bool decoded = false;
		if (encoded.packet.coding_mode ==
				GeometryCodingMode::IndependentMesh)
		{
			DracoData data(
				encoded.packet.payload.begin(),
				encoded.packet.payload.end());
			decoded = convert_draco_to_mesh(data, mesh);
			if (decoded)
			{
				// A full Draco mesh is self-describing. Normal EdgeBreaker
				// encoding may split source OBJ positions at UV/normal seams,
				// so older authored packets can report canonical source counts
				// that differ from the decoded point count. Accept the full
				// mesh here; any following position update still validates its
				// counts against this decoded topology before it is applied.
				m_topology_mesh = mesh;
				m_topology_id = encoded.packet.topology_id;
				m_topology_keyframe =
					encoded.packet.keyframe_frame_number;
				m_topology_generation = encoded.generation;
			}
		}
		else if (encoded.packet.coding_mode ==
			GeometryCodingMode::PositionUpdate)
		{
			if (m_topology_generation != encoded.generation ||
				m_topology_id != encoded.packet.topology_id ||
				m_topology_keyframe !=
					encoded.packet.keyframe_frame_number ||
				m_topology_mesh.verts.size() !=
					encoded.packet.vertex_count ||
				m_topology_mesh.indexes.size() / 3 !=
					encoded.packet.triangle_count)
			{
				// A seek or corrupt stream can expose a dependent sample before
				// its topology. Drop it and recover at the next keyframe.
				LOG(
					"GeometryDecoderDraco::decode - missing topology for "
					"frame=%u keyframe=%u",
					encoded.packet.frame_number,
					encoded.packet.keyframe_frame_number);
				m_decode_active.store(false, std::memory_order_release);
				return true;
			}
			decoded =
				convert_draco_update_to_mesh(encoded.packet, mesh);
		}

		if (!decoded)
		{
			m_decode_active.store(false, std::memory_order_release);
			m_decoded_meshes.set_error(
				"Draco geometry frame could not be decoded.");
			LOG(
				"GeometryDecoderDraco::decode - failed at pts=%f",
				encoded.presentation_time);
			return false;
		}

		MeshData mesh_data;
		mesh_data.mesh = std::move(mesh);
		mesh_data.generation = encoded.generation;
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
			LOG(
				"GeometryDecoderDraco::decode - decoded queue rejected pts=%f",
				encoded.presentation_time);
			return false;
		}
		m_decode_active.store(false, std::memory_order_release);
	}

	return true;
}
bool GeometryDecoderDraco::is_buffer_blocked()
{
	return m_decoded_meshes.full();
}
openvolumetric::FrameMatchResult GeometryDecoderDraco::get_mesh_data(
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
				meshes.pop_front();
				continue;
			}
			break;
		}
		// Discarding obsolete geometry is expected when presentation advances
		// across more than one sample. Avoid render-thread logging here.
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
				return openvolumetric::FrameMatchResult::Missing;
			}
			return openvolumetric::FrameMatchResult::NotReady;
		}
		if (meshes.front().presentation_time >
			presentation_time + tolerance)
		{
			LOG(
				"SYNC missing geometry for video pts=%f next_geometry=%f",
				presentation_time,
				meshes.front().presentation_time);
			return openvolumetric::FrameMatchResult::Missing;
		}
		mesh = meshes.front().mesh;
		actual_presentation_time = meshes.front().presentation_time;
		return openvolumetric::FrameMatchResult::Ready;
	});
}
bool GeometryDecoderDraco::convert_draco_to_mesh(DracoData& draco_data, Mesh& mesh_out)
{
	draco::DecoderBuffer buffer;
	buffer.Init(draco_data.data(), draco_data.size());
	
	draco::Mesh* mesh = nullptr;
	auto type_statusor = draco::Decoder::GetEncodedGeometryType(&buffer);
	if (!type_statusor.ok()) {
		return false;
	}

	const draco::EncodedGeometryType geom_type = type_statusor.value();
	if (geom_type != draco::TRIANGULAR_MESH)
	{
		return false;
	}

	draco::CycleTimer timer;
	timer.Start();

	draco::Decoder decoder;

	auto statusor = decoder.DecodeMeshFromBuffer(&buffer);
	if (!statusor.ok())
	{
		return false;
	}

	std::unique_ptr<draco::Mesh> in_mesh = std::move(statusor).value();
	timer.Stop();
	if (in_mesh)
	{
		mesh = in_mesh.get();
	}

	mesh_out.indexes.resize(mesh->num_faces() * 3);
	for (draco::FaceIndex face_id(0); face_id < mesh->num_faces(); ++face_id)
	{
		const draco::Mesh::Face face = mesh->face(face_id);
		memcpy(&mesh_out.indexes[0] + face_id.value() * 3,
			reinterpret_cast<const int*>(face.data()),
			sizeof(int) * 3);
	}

	mesh_out.verts.resize(mesh->num_points());

	const auto pos_att		= mesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
	const auto normal_att	= mesh->GetNamedAttribute(draco::GeometryAttribute::NORMAL);
	const auto uv_att		= mesh->GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
	if (pos_att == nullptr || normal_att == nullptr || uv_att == nullptr)
		return false;

	for (draco::PointIndex i(0); i < mesh->num_points(); ++i)
	{
		const draco::AttributeValueIndex pos_val_index = pos_att->mapped_index(i);
		if (!pos_att->ConvertValue<float, 3>(pos_val_index, &mesh_out.verts[i.value()].pos[0]))
		{
			return false;
		}

		const draco::AttributeValueIndex norm_val_index = normal_att->mapped_index(i);
		if (!normal_att->ConvertValue<float, 3>(norm_val_index, &mesh_out.verts[i.value()].normal[0]))
		{
			return false;
		}

		const draco::AttributeValueIndex uv_val_index = uv_att->mapped_index(i);
		if (!uv_att->ConvertValue<float, 2>(uv_val_index, &mesh_out.verts[i.value()].uv[0]))
		{
			return false;
		}
	}

	return true;
}

bool GeometryDecoderDraco::convert_draco_update_to_mesh(
	const GeometryPacket& packet,
	Mesh& mesh_out)
{
	std::vector<float> positions;
	std::string error;
	if (!decode_draco_point_cloud_positions(
		packet.payload.data(),
		packet.payload.size(),
		m_topology_mesh.verts.size(),
		positions,
		error))
	{
		LOG(
			"GeometryDecoderDraco::convert_draco_update_to_mesh - %s",
			error.c_str());
		return false;
	}

	mesh_out = m_topology_mesh;
	for (std::size_t vertex = 0; vertex < mesh_out.verts.size(); ++vertex)
	{
		for (int component = 0; component < 3; ++component)
		{
			mesh_out.verts[vertex].pos[component] =
				positions[vertex * 3 + component];
			mesh_out.verts[vertex].normal[component] = 0.0f;
		}
	}

	// Area-weighted face normals provide stable normals without transmitting a
	// second changing attribute in every dependent packet.
	for (std::size_t index = 0; index + 2 < mesh_out.indexes.size(); index += 3)
	{
		const int i0 = mesh_out.indexes[index];
		const int i1 = mesh_out.indexes[index + 1];
		const int i2 = mesh_out.indexes[index + 2];
		if (i0 < 0 || i1 < 0 || i2 < 0 ||
			static_cast<std::size_t>(i0) >= mesh_out.verts.size() ||
			static_cast<std::size_t>(i1) >= mesh_out.verts.size() ||
			static_cast<std::size_t>(i2) >= mesh_out.verts.size())
		{
			return false;
		}
		const float* a = mesh_out.verts[static_cast<std::size_t>(i0)].pos;
		const float* b = mesh_out.verts[static_cast<std::size_t>(i1)].pos;
		const float* c = mesh_out.verts[static_cast<std::size_t>(i2)].pos;
		const float ab[3]{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
		const float ac[3]{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
		const float normal[3]{
			ab[1] * ac[2] - ab[2] * ac[1],
			ab[2] * ac[0] - ab[0] * ac[2],
			ab[0] * ac[1] - ab[1] * ac[0]
		};
		for (const int vertex_index : {i0, i1, i2})
		{
			for (int component = 0; component < 3; ++component)
				mesh_out.verts[vertex_index].normal[component] +=
					normal[component];
		}
	}
	for (Vertex& vertex : mesh_out.verts)
	{
		const float length = std::sqrt(
			vertex.normal[0] * vertex.normal[0] +
			vertex.normal[1] * vertex.normal[1] +
			vertex.normal[2] * vertex.normal[2]);
		if (length > 1e-12f)
		{
			vertex.normal[0] /= length;
			vertex.normal[1] /= length;
			vertex.normal[2] /= length;
		}
	}
	return true;
}
bool GeometryDecoderDraco::stop_decoding()
{
	LOG("GeometryDecoderDraco::stop_decoding");

	this->m_decoder_state = STOP;

	if (m_decode_thread.joinable())
	{
		m_decode_thread.join();
	}

	return true;
}
void GeometryDecoderDraco::clear_frame_data()
{
	m_decoded_meshes.access([](auto& meshes)
	{
		if (!meshes.empty())
			meshes.pop_front();
	});
}
void GeometryDecoderDraco::flush_buffer()
{
	LOG("GeometryDecoderDraco::flush_buffer - start");

	m_decoded_meshes.clear();
	m_streamed_meshes.clear();

	LOG("GeometryDecoderDraco::flush_buffer - stop");
}

} // namespace openvolumetric
