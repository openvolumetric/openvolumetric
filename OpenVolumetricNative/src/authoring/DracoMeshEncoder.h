#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace openvolumetric::authoring
{

struct DracoEncodeOptions
{
	int position_quantization = 14;
	int normal_quantization = 10;
	int texture_quantization = 12;
	int encode_speed = 5;
	int decode_speed = 5;
	/// Uses Draco's sequential mesh codec so decoded point/index ordering
	/// matches canonical OBJ ordering for topology-aware position updates.
	bool preserve_point_order = false;
};

/// Loads a triangular OBJ mesh and writes an independently decodable Draco
/// bitstream using the linked library rather than the draco_encoder program.
bool encode_obj_to_draco(
	const std::filesystem::path& input_path,
	const std::filesystem::path& output_path,
	const DracoEncodeOptions& options,
	std::string& error);

/// Loads and encodes an OBJ directly into memory. This lets the packer choose
/// the optimal Draco mesh method after topology analysis without creating a
/// second set of temporary files.
bool encode_obj_to_draco(
	const std::filesystem::path& input_path,
	const DracoEncodeOptions& options,
	std::vector<std::uint8_t>& output,
	std::string& error);

} // namespace openvolumetric::authoring
