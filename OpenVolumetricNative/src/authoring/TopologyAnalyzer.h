#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace openvolumetric::authoring
{

/// Controls deterministic OBJ canonicalization and topology identity.
struct TopologyOptions
{
	/// Fractional bits used before UV values enter the topology fingerprint.
	/// Values that quantize to the same integer are treated as equivalent.
	int uv_quantization_bits = 12;
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

/// Loads one OBJ and creates the canonical representation used for topology
/// comparison. Returns false with a human-readable error for malformed input.
bool load_canonical_obj(
	const std::filesystem::path& input_path,
	const TopologyOptions& options,
	CanonicalMesh& output,
	std::string& error);

/// Performs an exact comparison after fingerprint matching. This protects the
/// format decision from relying on hash collision resistance alone.
bool topology_matches(
	const CanonicalMesh& left,
	const CanonicalMesh& right);

} // namespace openvolumetric::authoring
