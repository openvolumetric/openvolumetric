#include "TopologyAnalyzer.h"

#include <draco/core/decoder_buffer.h>
#include <draco/io/obj_decoder.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>
#include <type_traits>

namespace openvolumetric::authoring
{
namespace
{

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

struct RenderVertexKey
{
	std::uint32_t position = 0;
	std::int64_t texcoord = -1;

	bool operator<(const RenderVertexKey& other) const
	{
		return std::tie(position, texcoord) <
			std::tie(other.position, other.texcoord);
	}
};

void hash_byte(std::uint64_t& hash, std::uint8_t value)
{
	hash ^= value;
	hash *= kFnvPrime;
}

template <typename Integer>
void hash_integer(std::uint64_t& hash, Integer value)
{
	using Unsigned = std::make_unsigned_t<Integer>;
	const Unsigned bits = static_cast<Unsigned>(value);
	for (std::size_t byte = 0; byte < sizeof(Integer); ++byte)
	{
		hash_byte(
			hash,
			static_cast<std::uint8_t>(bits >> (byte * 8)));
	}
}

std::uint64_t calculate_topology_id(const CanonicalMesh& mesh)
{
	std::uint64_t hash = kFnvOffsetBasis;

	// The identity schema version makes future canonicalization changes
	// explicit even if all following values happen to match.
	hash_integer(hash, std::uint32_t{1});
	hash_integer(hash, static_cast<std::uint64_t>(mesh.vertex_count()));
	hash_integer(hash, static_cast<std::uint64_t>(mesh.triangle_count()));
	hash_integer(hash, static_cast<std::uint8_t>(mesh.has_texcoords));
	hash_integer(hash, static_cast<std::uint8_t>(mesh.has_normals));

	for (const std::uint32_t index : mesh.triangle_indices)
		hash_integer(hash, index);
	for (const std::int64_t uv : mesh.quantized_uvs)
		hash_integer(hash, uv);
	return hash;
}

bool validate_options(
	const TopologyAnalysisOptions& options,
	std::string& error)
{
	if (options.uv_quantization_bits < 0 ||
		options.uv_quantization_bits > 30)
	{
		error = "UV topology quantization must be between 0 and 30 bits.";
		return false;
	}
	if (options.position_quantization_bits < 1 ||
		options.position_quantization_bits > 30)
	{
		error = "Position estimate quantization must be between 1 and 30 bits.";
		return false;
	}
	return true;
}

bool read_obj(
	const std::filesystem::path& input_path,
	draco::Mesh& mesh,
	std::string& error)
{
	if (!std::filesystem::is_regular_file(input_path))
	{
		error = "OBJ input does not exist: " + input_path.string();
		return false;
	}

	std::ifstream input(input_path, std::ios::binary);
	if (!input)
	{
		error = "Could not open OBJ input: " + input_path.string();
		return false;
	}
	const std::string obj_bytes{
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>()};
	if (obj_bytes.empty())
	{
		error = "OBJ input is empty: " + input_path.string();
		return false;
	}

	draco::ObjDecoder decoder;
	draco::DecoderBuffer input_buffer;
	input_buffer.Init(obj_bytes.data(), obj_bytes.size());
	const draco::Status status =
		decoder.DecodeFromBuffer(&input_buffer, &mesh);
	if (!status.ok())
	{
		error = "Could not load OBJ " + input_path.string() + ": " +
			status.error_msg_string();
		return false;
	}
	if (mesh.num_faces() == 0 || mesh.num_points() == 0)
	{
		error = "OBJ contains no triangular mesh data: " +
			input_path.string();
		return false;
	}
	return true;
}

std::uint64_t packed_position_bytes(
	std::size_t vertex_count,
	int bits_per_component)
{
	const std::uint64_t component_count =
		static_cast<std::uint64_t>(vertex_count) * 3;
	const std::uint64_t bit_count =
		component_count * static_cast<std::uint64_t>(bits_per_component);
	return (bit_count + 7) / 8;
}

} // namespace

bool load_canonical_obj(
	const std::filesystem::path& input_path,
	const TopologyAnalysisOptions& options,
	CanonicalMesh& output,
	std::string& error)
{
	error.clear();
	output = {};
	if (!validate_options(options, error))
		return false;

	draco::Mesh mesh;
	if (!read_obj(input_path, mesh, error))
		return false;

	const draco::PointAttribute* positions =
		mesh.GetNamedAttribute(draco::GeometryAttribute::POSITION);
	const draco::PointAttribute* texcoords =
		mesh.GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
	const draco::PointAttribute* normals =
		mesh.GetNamedAttribute(draco::GeometryAttribute::NORMAL);
	if (positions == nullptr || positions->num_components() < 3)
	{
		error = "OBJ has no three-component position attribute: " +
			input_path.string();
		return false;
	}
	if (texcoords != nullptr && texcoords->num_components() < 2)
	{
		error = "OBJ texture coordinates have fewer than two components: " +
			input_path.string();
		return false;
	}

	output.has_texcoords = texcoords != nullptr;
	output.has_normals = normals != nullptr;
	output.triangle_indices.reserve(
		static_cast<std::size_t>(mesh.num_faces()) * 3);

	std::map<RenderVertexKey, std::uint32_t> render_vertices;
	const double uv_scale = std::ldexp(1.0, options.uv_quantization_bits);

	for (std::uint32_t face_index = 0;
		face_index < mesh.num_faces();
		++face_index)
	{
		const draco::Mesh::Face& face =
			mesh.face(draco::FaceIndex(face_index));
		for (const draco::PointIndex point : face)
		{
			const draco::AttributeValueIndex position_index =
				positions->mapped_index(point);
			const draco::AttributeValueIndex texcoord_index =
				texcoords == nullptr
					? draco::AttributeValueIndex(-1)
					: texcoords->mapped_index(point);
			if (position_index.value() < 0 ||
				(texcoords != nullptr && texcoord_index.value() < 0))
			{
				error = "OBJ contains an invalid corner attribute mapping: " +
					input_path.string();
				return false;
			}

			const RenderVertexKey key{
				static_cast<std::uint32_t>(position_index.value()),
				static_cast<std::int64_t>(texcoord_index.value())
			};
			const auto found = render_vertices.find(key);
			if (found != render_vertices.end())
			{
				output.triangle_indices.push_back(found->second);
				continue;
			}

			if (render_vertices.size() >=
				static_cast<std::size_t>(
					std::numeric_limits<std::uint32_t>::max()))
			{
				error = "Canonical OBJ has too many render vertices: " +
					input_path.string();
				return false;
			}

			float position[3]{};
			if (!positions->ConvertValue<float, 3>(
				position_index, position))
			{
				error = "Could not convert an OBJ position: " +
					input_path.string();
				return false;
			}

			const std::uint32_t render_index =
				static_cast<std::uint32_t>(render_vertices.size());
			render_vertices.emplace(key, render_index);
			output.triangle_indices.push_back(render_index);
			output.positions.insert(
				output.positions.end(),
				std::begin(position),
				std::end(position));

			if (texcoords != nullptr)
			{
				double uv[2]{};
				if (!texcoords->ConvertValue<double, 2>(
					texcoord_index, uv) ||
					!std::isfinite(uv[0]) ||
					!std::isfinite(uv[1]))
				{
					error = "Could not convert finite OBJ texture coordinates: " +
						input_path.string();
					return false;
				}
				for (const double component : uv)
				{
					const double scaled = component * uv_scale;
					if (scaled <
							static_cast<double>(
								std::numeric_limits<std::int64_t>::min()) ||
						scaled >
							static_cast<double>(
								std::numeric_limits<std::int64_t>::max()))
					{
						error = "OBJ texture coordinate is outside the supported range: " +
							input_path.string();
						return false;
					}
					output.quantized_uvs.push_back(
						static_cast<std::int64_t>(std::llround(scaled)));
				}
			}
		}
	}

	output.topology_id = calculate_topology_id(output);
	return true;
}

bool topology_matches(
	const CanonicalMesh& left,
	const CanonicalMesh& right)
{
	return
		left.topology_id == right.topology_id &&
		left.vertex_count() == right.vertex_count() &&
		left.triangle_count() == right.triangle_count() &&
		left.has_texcoords == right.has_texcoords &&
		left.has_normals == right.has_normals &&
		left.triangle_indices == right.triangle_indices &&
		left.quantized_uvs == right.quantized_uvs;
}

bool analyze_obj_sequence(
	const std::vector<std::filesystem::path>& input_paths,
	const TopologyAnalysisOptions& options,
	TopologyAnalysisReport& report,
	std::string& error)
{
	error.clear();
	report = {};
	if (!validate_options(options, error))
		return false;
	if (input_paths.empty())
	{
		error = "At least one OBJ is required for topology analysis.";
		return false;
	}

	CanonicalMesh run_topology;
	for (std::size_t frame = 0; frame < input_paths.size(); ++frame)
	{
		CanonicalMesh mesh;
		if (!load_canonical_obj(input_paths[frame], options, mesh, error))
			return false;

		const std::uint64_t full_position_bytes =
			static_cast<std::uint64_t>(mesh.vertex_count()) *
			3 * sizeof(float);
		const std::uint64_t reused_position_bytes =
			packed_position_bytes(
				mesh.vertex_count(),
				options.position_quantization_bits);
		report.independent_position_bytes += full_position_bytes;

		if (report.runs.empty() ||
			!topology_matches(run_topology, mesh))
		{
			run_topology = mesh;
			TopologyRun run;
			run.first_frame = frame;
			run.frame_count = 1;
			run.topology_id = mesh.topology_id;
			run.vertex_count = mesh.vertex_count();
			run.triangle_count = mesh.triangle_count();
			run.independent_position_bytes = full_position_bytes;

			// A new topology begins with a complete position keyframe.
			run.estimated_reused_position_bytes = full_position_bytes;
			report.runs.push_back(run);
		}
		else
		{
			TopologyRun& run = report.runs.back();
			++run.frame_count;
			run.independent_position_bytes += full_position_bytes;
			run.estimated_reused_position_bytes += reused_position_bytes;
			++report.reusable_frame_count;
		}
	}

	report.frame_count = input_paths.size();
	for (const TopologyRun& run : report.runs)
	{
		report.estimated_reused_position_bytes +=
			run.estimated_reused_position_bytes;
	}
	return true;
}

std::string format_topology_analysis(const TopologyAnalysisReport& report)
{
	std::ostringstream output;
	output << "Topology analysis: " << report.frame_count << " frames, "
		<< report.runs.size() << " runs, "
		<< report.reusable_frame_count << " reusable frames";
	if (report.independent_position_bytes > 0)
	{
		const double ratio =
			static_cast<double>(report.estimated_reused_position_bytes) /
			static_cast<double>(report.independent_position_bytes);
		output << ", estimated position payload "
			<< report.estimated_reused_position_bytes << "/"
			<< report.independent_position_bytes << " bytes ("
			<< std::fixed << std::setprecision(1)
			<< ratio * 100.0 << "%)";
	}

	for (const TopologyRun& run : report.runs)
	{
		output << "\n  frames " << run.first_frame << "-"
			<< run.first_frame + run.frame_count - 1
			<< ": " << run.frame_count << " frame(s), topology 0x"
			<< std::hex << std::setw(16) << std::setfill('0')
			<< run.topology_id << std::dec << std::setfill(' ')
			<< ", " << run.vertex_count << " vertices, "
			<< run.triangle_count << " triangles";
	}
	return output.str();
}

} // namespace openvolumetric::authoring
