#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace openvolumetric::authoring
{

/// Options used by the non-destructive topology analysis pass.
struct TopologyAnalysisOptions
{
	/// Fractional bits used before UV values enter the topology fingerprint.
	/// Values that quantize to the same integer are treated as equivalent.
	int uv_quantization_bits = 12;

	/// Estimated bits per position component for a future residual payload.
	int position_quantization_bits = 14;
};

/// Canonical render-mesh data extracted from one OBJ.
///
/// OBJ position/UV corner tuples are expanded into deterministic render
/// vertices. Positions deliberately do not participate in topology identity.
struct CanonicalMesh
{
	std::vector<float> positions;
	std::vector<std::uint32_t> triangle_indices;
	std::vector<std::int64_t> quantized_uvs;
	bool has_texcoords = false;
	bool has_normals = false;
	std::uint64_t topology_id = 0;

	std::size_t vertex_count() const
	{
		return positions.size() / 3;
	}

	std::size_t triangle_count() const
	{
		return triangle_indices.size() / 3;
	}
};

/// One consecutive sequence region whose canonical topology and UVs match.
struct TopologyRun
{
	std::size_t first_frame = 0;
	std::size_t frame_count = 0;
	std::uint64_t topology_id = 0;
	std::size_t vertex_count = 0;
	std::size_t triangle_count = 0;
	std::uint64_t independent_position_bytes = 0;
	std::uint64_t estimated_reused_position_bytes = 0;
};

/// Aggregate analysis used to decide whether temporal geometry coding is
/// worth attempting before the packet format or runtime is changed.
struct TopologyAnalysisReport
{
	std::size_t frame_count = 0;
	std::size_t reusable_frame_count = 0;
	std::uint64_t independent_position_bytes = 0;
	std::uint64_t estimated_reused_position_bytes = 0;
	std::vector<TopologyRun> runs;
};

/// Loads one OBJ and creates the canonical representation used for topology
/// comparison. Returns false with a human-readable error for malformed input.
bool load_canonical_obj(
	const std::filesystem::path& input_path,
	const TopologyAnalysisOptions& options,
	CanonicalMesh& output,
	std::string& error);

/// Performs an exact comparison after fingerprint matching. This protects the
/// format decision from relying on hash collision resistance alone.
bool topology_matches(
	const CanonicalMesh& left,
	const CanonicalMesh& right);

/// Analyses an ordered OBJ sequence and segments consecutive matching frames.
bool analyze_obj_sequence(
	const std::vector<std::filesystem::path>& input_paths,
	const TopologyAnalysisOptions& options,
	TopologyAnalysisReport& report,
	std::string& error);

/// Produces a concise, stable text summary suitable for editor logs and
/// benchmark records.
std::string format_topology_analysis(const TopologyAnalysisReport& report);

} // namespace openvolumetric::authoring
