#pragma once

#include <filesystem>
#include <string>

namespace openvol::authoring
{

struct DracoEncodeOptions
{
	int position_quantization = 14;
	int normal_quantization = 10;
	int texture_quantization = 12;
	int encode_speed = 5;
	int decode_speed = 5;
};

/// Loads a triangular OBJ mesh and writes an independently decodable Draco
/// bitstream using the linked library rather than the draco_encoder program.
bool encode_obj_to_draco(
	const std::filesystem::path& input_path,
	const std::filesystem::path& output_path,
	const DracoEncodeOptions& options,
	std::string& error);

} // namespace openvol::authoring
