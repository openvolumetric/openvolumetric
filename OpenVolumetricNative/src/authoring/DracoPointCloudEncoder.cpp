#include "DracoPointCloudEncoder.h"

#include <draco/compression/encode.h>
#include <draco/point_cloud/point_cloud_builder.h>

namespace openvolumetric::authoring
{

bool encode_positions_to_draco_point_cloud(
	const std::vector<float>& positions,
	int quantization_bits,
	int encode_speed,
	int decode_speed,
	std::vector<std::uint8_t>& output,
	std::string& error)
{
	output.clear();
	error.clear();
	if (positions.empty() || positions.size() % 3 != 0)
	{
		error = "Position data must contain complete XYZ triples.";
		return false;
	}
	if (quantization_bits < 1 || quantization_bits > 30 ||
		encode_speed < 0 || encode_speed > 10 ||
		decode_speed < 0 || decode_speed > 10)
	{
		error = "Draco quantization must be 1-30 and speeds must be 0-10.";
		return false;
	}

	draco::PointCloudBuilder builder;
	builder.Start(static_cast<std::uint32_t>(positions.size() / 3));
	const int position_attribute = builder.AddAttribute(
		draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32);
	builder.SetAttributeValuesForAllPoints(
		position_attribute, positions.data(), sizeof(float) * 3);
	std::unique_ptr<draco::PointCloud> point_cloud =
		builder.Finalize(false);
	if (!point_cloud)
	{
		error = "Draco could not construct the position point cloud.";
		return false;
	}

	draco::Encoder encoder;
	encoder.SetSpeedOptions(encode_speed, decode_speed);
	encoder.SetAttributeQuantization(
		draco::GeometryAttribute::POSITION, quantization_bits);
	encoder.SetEncodingMethod(draco::POINT_CLOUD_SEQUENTIAL_ENCODING);
	draco::EncoderBuffer buffer;
	const draco::Status status =
		encoder.EncodePointCloudToBuffer(*point_cloud, &buffer);
	if (!status.ok() || buffer.size() == 0)
	{
		error = "Could not encode Draco position point cloud: " +
			status.error_msg_string();
		return false;
	}
	output.assign(buffer.data(), buffer.data() + buffer.size());
	return true;
}

} // namespace openvolumetric::authoring
