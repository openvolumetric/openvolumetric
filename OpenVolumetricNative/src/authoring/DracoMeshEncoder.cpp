#include "DracoMeshEncoder.h"

#include <draco/compression/encode.h>
#include <draco/core/decoder_buffer.h>
#include <draco/io/obj_decoder.h>

#include <fstream>
#include <iterator>

namespace openvolumetric::authoring
{

bool encode_obj_to_draco(
	const std::filesystem::path& input_path,
	const std::filesystem::path& output_path,
	const DracoEncodeOptions& options,
	std::string& error)
{
	error.clear();
	if (!std::filesystem::is_regular_file(input_path))
	{
		error = "OBJ input does not exist: " + input_path.string();
		return false;
	}
	if (options.position_quantization < 1 ||
		options.position_quantization > 30 ||
		options.normal_quantization < 1 ||
		options.normal_quantization > 30 ||
		options.texture_quantization < 1 ||
		options.texture_quantization > 30 ||
		options.encode_speed < 0 ||
		options.encode_speed > 10 ||
		options.decode_speed < 0 ||
		options.decode_speed > 10)
	{
		error =
			"Draco quantization must be 1-30 and speed values must be 0-10.";
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
		error = "OBJ input is empty.";
		return false;
	}

	draco::Mesh mesh;
	draco::ObjDecoder decoder;
	draco::DecoderBuffer input_buffer;
	input_buffer.Init(obj_bytes.data(), obj_bytes.size());
	const draco::Status decode_status =
		decoder.DecodeFromBuffer(&input_buffer, &mesh);
	if (!decode_status.ok())
	{
		error = "Could not load OBJ: " + decode_status.error_msg_string();
		return false;
	}
	if (mesh.num_faces() == 0 || mesh.num_points() == 0)
	{
		error = "OBJ contains no triangular mesh data.";
		return false;
	}

	draco::Encoder encoder;
	encoder.SetSpeedOptions(options.encode_speed, options.decode_speed);
	encoder.SetAttributeQuantization(
		draco::GeometryAttribute::POSITION,
		options.position_quantization);
	encoder.SetAttributeQuantization(
		draco::GeometryAttribute::NORMAL,
		options.normal_quantization);
	encoder.SetAttributeQuantization(
		draco::GeometryAttribute::TEX_COORD,
		options.texture_quantization);

	draco::EncoderBuffer buffer;
	const draco::Status encode_status =
		encoder.EncodeMeshToBuffer(mesh, &buffer);
	if (!encode_status.ok() || buffer.size() == 0)
	{
		error = "Could not encode Draco mesh: " +
			encode_status.error_msg_string();
		return false;
	}

	std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
	if (!output ||
		!output.write(
			buffer.data(),
			static_cast<std::streamsize>(buffer.size())))
	{
		error = "Could not write Draco output: " + output_path.string();
		return false;
	}
	return true;
}

} // namespace openvolumetric::authoring
