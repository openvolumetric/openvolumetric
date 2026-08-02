#include <DracoMeshEncoder.h>
#include <VolumetricVideoPacker.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

std::string obj_frame(float x_offset)
{
	return
		"v " + std::to_string(x_offset) + " 0 0\n"
		"v " + std::to_string(1.0F + x_offset) + " 0 0\n"
		"v " + std::to_string(x_offset) + " 1 0\n"
		"vt 0 0\n"
		"vt 1 0\n"
		"vt 0 1\n"
		"vn 0 0 1\n"
		"f 1/1/1 2/2/1 3/3/1\n";
}

bool write_text(const std::filesystem::path& path, const std::string& text)
{
	std::ofstream output(path, std::ios::binary | std::ios::trunc);
	output.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(output);
}

bool corrupt_first_position_update(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	std::vector<unsigned char> bytes(
		(std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	for (std::size_t index = 0; index + 44 < bytes.size(); ++index)
	{
		if (bytes[index] == 'V' && bytes[index + 1] == 'V' &&
			bytes[index + 2] == 'G' && bytes[index + 3] == 'F' &&
			bytes[index + 8] == 1)
		{
			// Preserve packet framing but make the Draco point-cloud payload
			// undecodable. The following independent sample remains intact.
			bytes[index + 40] ^= 0xff;
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output.write(
				reinterpret_cast<const char*>(bytes.data()),
				static_cast<std::streamsize>(bytes.size()));
			return static_cast<bool>(output);
		}
	}
	return false;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc < 4 || argc > 6)
	{
		std::cerr << "Usage: OpenVolumetricFixtureGenerator "
			"<media.mp4> <work-directory> <output.mp4> "
			"[temporal|independent] [fragment-seconds]\n";
		return 2;
	}

	const std::filesystem::path media = argv[1];
	const std::filesystem::path work = argv[2];
	const std::filesystem::path output = argv[3];
	const std::string mode = argc < 5 ? "temporal" : argv[4];
	const bool corrupt = mode == "corrupt";
	const bool temporal = mode == "temporal" || corrupt;
	if (!temporal && mode != "independent")
	{
		std::cerr << "Geometry mode must be temporal, independent, or corrupt.\n";
		return 2;
	}
	const std::uint32_t fragment_seconds = argc == 6
		? static_cast<std::uint32_t>(std::stoul(argv[5]))
		: 0;
	const std::filesystem::path objects = work / "objects";
	const std::filesystem::path geometry = work / "geometry";
	std::error_code error_code;
	if (std::filesystem::exists(work, error_code))
	{
		std::cerr << "Fixture work directory must not already exist: "
			<< work << '\n';
		return 1;
	}
	std::filesystem::create_directories(objects, error_code);
	std::filesystem::create_directories(geometry, error_code);
	if (error_code)
	{
		std::cerr << "Could not create fixture work directory: "
			<< error_code.message() << '\n';
		return 1;
	}

	openvolumetric::authoring::DracoEncodeOptions draco;
	draco.preserve_point_order = true;
	for (int frame = 0; frame < 4; ++frame)
	{
		char name[16]{};
		std::snprintf(name, sizeof(name), "%06d", frame);
		const std::filesystem::path object = objects / (std::string(name) + ".obj");
		const std::filesystem::path encoded =
			geometry / (std::string(name) + ".drc");
		if (!write_text(object, obj_frame(static_cast<float>(frame) * 0.05F)))
		{
			std::cerr << "Could not write " << object << '\n';
			return 1;
		}
		std::string error;
		if (!openvolumetric::authoring::encode_obj_to_draco(
				object, encoded, draco, error))
		{
			std::cerr << error << '\n';
			return 1;
		}
	}

	std::filesystem::remove(output, error_code);
	openvolumetric::authoring::PackOptions options;
	options.media_path = media;
	options.geometry_directory = geometry;
	options.source_geometry_directory = objects;
	options.output_path = output;
	options.enable_topology_compression = temporal;
	options.maximum_geometry_keyframe_interval = 2;
	options.fragment_duration_seconds = fragment_seconds;
	options.fragment_frame_interval = fragment_seconds * 4;
	options.draco_options = draco;
	openvolumetric::authoring::PackStatistics statistics;
	if (!openvolumetric::authoring::pack_openvolumetric(options, &statistics))
	{
		std::cerr << "OpenVolumetric fixture packaging failed.\n";
		return 1;
	}
	if (corrupt && !corrupt_first_position_update(output))
	{
		std::cerr << "Could not corrupt the dependent geometry fixture.\n";
		return 1;
	}

	std::cout << "Created " << output << " with " << statistics.frame_count
		<< " geometry samples.\n";
	return 0;
}
