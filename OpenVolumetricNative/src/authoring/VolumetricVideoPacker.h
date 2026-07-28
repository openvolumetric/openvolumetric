#pragma once

#include <filesystem>

namespace openvolumetric::authoring
{

/// Inputs for adding timed Draco samples to an existing video/audio MP4.
struct PackOptions
{
	std::filesystem::path media_path;
	std::filesystem::path geometry_directory;
	std::filesystem::path output_path;
};

/// Creates and verifies a combined volumetric MP4.
///
/// The output is created only after its vvge samples, timestamps, payloads,
/// and seek behaviour pass round-trip verification. Existing outputs are not
/// overwritten.
bool pack_openvolumetric(const PackOptions& options);

} // namespace openvolumetric::authoring
