#include "DracoPointCloudDecoder.h"

#include <draco/attributes/point_attribute.h>
#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>

namespace openvolumetric
{

bool decode_draco_point_cloud_positions(
	const std::uint8_t* data,
	std::size_t size,
	std::size_t expected_vertex_count,
	std::vector<float>& positions,
	std::string& error)
{
	positions.clear();
	error.clear();
	if (data == nullptr || size == 0 || expected_vertex_count == 0)
	{
		error = "Draco position update input is empty.";
		return false;
	}

	draco::DecoderBuffer buffer;
	buffer.Init(reinterpret_cast<const char*>(data), size);
	auto type = draco::Decoder::GetEncodedGeometryType(&buffer);
	if (!type.ok() || type.value() != draco::POINT_CLOUD)
	{
		error = "Geometry update is not a Draco point cloud.";
		return false;
	}

	draco::Decoder decoder;
	auto result = decoder.DecodePointCloudFromBuffer(&buffer);
	if (!result.ok())
	{
		error = "Could not decode Draco position update: " +
			result.status().error_msg_string();
		return false;
	}
	std::unique_ptr<draco::PointCloud> point_cloud =
		std::move(result).value();
	if (!point_cloud ||
		static_cast<std::size_t>(point_cloud->num_points()) !=
			expected_vertex_count)
	{
		error = "Draco position update vertex count does not match its keyframe.";
		return false;
	}
	const draco::PointAttribute* attribute =
		point_cloud->GetNamedAttribute(draco::GeometryAttribute::POSITION);
	if (!attribute || attribute->num_components() != 3)
	{
		error = "Draco position update has no three-component position attribute.";
		return false;
	}

	positions.resize(expected_vertex_count * 3);
	for (draco::PointIndex point(0); point < point_cloud->num_points(); ++point)
	{
		attribute->GetMappedValue(
			point,
			positions.data() + static_cast<std::size_t>(point.value()) * 3);
	}
	return true;
}

} // namespace openvolumetric
