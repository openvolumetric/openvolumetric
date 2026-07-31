#pragma once

#include "DracoMeshEncoder.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace openvolumetric::authoring
{

/// Inputs for adding timed Draco samples to an existing video/audio MP4.
struct PackOptions
{
	std::filesystem::path media_path;
	std::filesystem::path geometry_directory;
	/// Matching OBJ sequence used to identify topology and populate packet
	/// validation metadata for both coding strategies.
	std::filesystem::path source_geometry_directory;
	std::filesystem::path output_path;
	bool enable_topology_compression = true;
	/// Maximum samples in one topology-reference window. Zero leaves the
	/// window unbounded until topology changes.
	std::uint32_t maximum_geometry_keyframe_interval = 0;
	/// Zero writes conventional fast-start MP4. Values 1, 2, or 4 write a
	/// fragmented MP4 and force full geometry at each duration boundary.
	std::uint32_t fragment_duration_seconds = 0;
	/// Integral source-frame interval corresponding to fragment duration.
	std::uint32_t fragment_frame_interval = 0;
	/// Settings reused when singleton topology groups are re-encoded with
	/// Draco's normal mesh method after topology analysis.
	DracoEncodeOptions draco_options;
};

/// Summary returned to Editor integrations for their visible encoding log.
struct PackStatistics
{
	std::size_t frame_count = 0;
	std::size_t independent_mesh_count = 0;
	std::size_t position_update_count = 0;
	std::uint64_t independent_payload_bytes = 0;
	std::uint64_t authored_payload_bytes = 0;
	std::uint64_t packet_header_bytes = 0;
	std::size_t fragment_count = 0;
};

/// Creates and verifies a combined volumetric MP4.
///
/// The output is created only after its vvge samples, timestamps, payloads,
/// and seek behaviour pass round-trip verification. Existing outputs are not
/// overwritten.
bool pack_openvolumetric(
	const PackOptions& options,
	PackStatistics* statistics = nullptr);

} // namespace openvolumetric::authoring
