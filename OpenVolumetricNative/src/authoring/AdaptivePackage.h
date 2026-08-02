#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace openvolumetric::authoring
{

/** Metadata known by the encoder for one completed representation. */
struct AdaptivePackageRepresentation
{
	std::string id;
	std::filesystem::path resource_path;
	std::string compatibility_group;
	std::uint32_t position_quantization_bits = 0;
	std::uint64_t geometry_payload_bytes = 0;
	bool temporal_compression = false;
};

/** Inputs for validating two or more outputs and writing their shared manifest. */
struct AdaptivePackageOptions
{
	std::string presentation_id;
	std::filesystem::path manifest_path;
	double segment_duration_seconds = 0.0;
	std::vector<AdaptivePackageRepresentation> representations;
};

/** Measurements produced while validating a completed adaptive package. */
struct AdaptivePackageVerification
{
	std::size_t representation_count = 0;
	std::size_t segment_count = 0;
	std::size_t geometry_sample_count = 0;
	std::size_t checked_switch_boundary_count = 0;
	double duration_seconds = 0.0;
	double frame_rate = 0.0;
	bool has_audio = false;
	std::uint32_t audio_sample_rate = 0;
	std::uint32_t audio_channels = 0;

	/** Returns a concise engine-facing success report. */
	std::string summary() const;
};

/**
 * Probes completed fragmented MP4 files, rejects misaligned representations,
 * and atomically writes an OpenVolumetric adaptive manifest version 1.
 */
bool write_adaptive_package_manifest(
	const AdaptivePackageOptions& options,
	std::string& error,
	AdaptivePackageVerification* verification = nullptr);

} // namespace openvolumetric::authoring
