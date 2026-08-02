#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <AdaptiveManifest.h>
#include <AdaptivePolicy.h>
#include <AdaptiveSelection.h>
#include <FragmentedMp4Index.h>
#include <GeometryPacket.h>
#include <LocalFileByteSource.h>
#include <OpenVolumetricPlayer.h>
#include <TopologyAnalyzer.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

using namespace openvolumetric;

class TemporaryFile final
{
public:
	TemporaryFile(std::string extension, const std::string& contents)
	{
		static std::atomic<std::uint64_t> sequence{0};
		const auto timestamp = std::chrono::steady_clock::now()
			.time_since_epoch().count();
		m_path = std::filesystem::temp_directory_path() /
			("openvolumetric-test-" + std::to_string(timestamp) + "-" +
			 std::to_string(sequence.fetch_add(1)) + std::move(extension));
		std::ofstream output(m_path, std::ios::binary | std::ios::trunc);
		output.write(
			contents.data(), static_cast<std::streamsize>(contents.size()));
		if (!output)
			throw std::runtime_error("Could not create native test input.");
	}

	~TemporaryFile()
	{
		std::error_code ignored;
		std::filesystem::remove(m_path, ignored);
	}

	TemporaryFile(const TemporaryFile&) = delete;
	TemporaryFile& operator=(const TemporaryFile&) = delete;

	const std::filesystem::path& path() const { return m_path; }

private:
	std::filesystem::path m_path;
};

const char* kValidManifest = R"json(
{
  "format": "openvolumetric-adaptive",
  "version": 1,
  "presentation_id": "test-presentation",
  "duration_seconds": 4.0,
  "segment_duration_seconds": 2.0,
  "has_audio": true,
  "segments": [
    {"number": 0, "start_seconds": 0.0, "duration_seconds": 2.0},
    {"number": 1, "start_seconds": 2.0, "duration_seconds": 2.0}
  ],
  "representations": [
    {
      "id": "low",
      "resource_uri": "low.mp4",
      "compatibility_group": "main",
      "bandwidth": 1200000,
      "texture": {
        "codec": "hvc1",
        "width": 512,
        "height": 512,
        "bitrate": 900000
      },
      "geometry": {
        "codec": "vvge-v2",
        "position_quantization_bits": 11,
        "bitrate": 200000,
        "temporal_compression": true
      }
    },
    {
      "id": "high",
      "resource_uri": "high.mp4",
      "compatibility_group": "main",
      "bandwidth": 4800000,
      "texture": {
        "codec": "hvc1",
        "width": 1024,
        "height": 1024,
        "bitrate": 4000000
      },
      "geometry": {
        "codec": "vvge-v2",
        "position_quantization_bits": 14,
        "bitrate": 600000,
        "temporal_compression": true
      }
    }
  ]
}
)json";

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
	bytes.push_back(static_cast<std::uint8_t>(value >> 24));
	bytes.push_back(static_cast<std::uint8_t>(value >> 16));
	bytes.push_back(static_cast<std::uint8_t>(value >> 8));
	bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_type(std::vector<std::uint8_t>& bytes, const char (&type)[5])
{
	bytes.insert(bytes.end(), type, type + 4);
}

void append_tfra_entry(
	std::vector<std::uint8_t>& bytes,
	std::uint32_t time,
	std::uint32_t offset)
{
	append_u32(bytes, time);
	append_u32(bytes, offset);
	bytes.push_back(1);
	bytes.push_back(1);
	bytes.push_back(1);
}

std::vector<std::uint8_t> make_fragment_index()
{
	constexpr std::uint32_t tfra_size = 46;
	constexpr std::uint32_t mfra_size = 70;
	std::vector<std::uint8_t> bytes;
	bytes.reserve(mfra_size);
	append_u32(bytes, mfra_size);
	append_type(bytes, "mfra");
	append_u32(bytes, tfra_size);
	append_type(bytes, "tfra");
	append_u32(bytes, 0); // version and flags
	append_u32(bytes, 1); // track ID
	append_u32(bytes, 0); // one-byte traf, trun, and sample numbers
	append_u32(bytes, 2); // entry count
	append_tfra_entry(bytes, 0, 100);
	append_tfra_entry(bytes, 2000, 500);
	append_u32(bytes, 16);
	append_type(bytes, "mfro");
	append_u32(bytes, 0); // version and flags
	append_u32(bytes, mfra_size);
	return bytes;
}

std::string triangle_obj(
	const char* second_position,
	const char* second_uv,
	bool reverse_winding = false)
{
	std::string result =
		"v 0 0 0\n"
		"v " + std::string(second_position) + "\n"
		"v 0 1 0\n"
		"vt 0 0\n"
		"vt " + std::string(second_uv) + "\n"
		"vt 0 1\n"
		"vn 0 0 1\n";
	result += reverse_winding
		? "f 1/1/1 3/3/1 2/2/1\n"
		: "f 1/1/1 2/2/1 3/3/1\n";
	return result;
}

bool wait_for_presentation(
	OpenVolumetricPlayer& player,
	double requested_time,
	OpenVolumetricPresentation& presentation)
{
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (player.presentation(requested_time, presentation) ==
			FrameMatchResult::Ready)
		{
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return false;
}

FrameMatchResult wait_for_terminal_presentation_result(
	OpenVolumetricPlayer& player,
	double requested_time,
	OpenVolumetricPresentation& presentation)
{
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::seconds(3);
	while (std::chrono::steady_clock::now() < deadline)
	{
		const FrameMatchResult result =
			player.presentation(requested_time, presentation);
		if (result != FrameMatchResult::NotReady)
			return result;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return FrameMatchResult::NotReady;
}

} // namespace

TEST_CASE("independent geometry packets round trip")
{
	GeometryPacket input;
	input.frame_number = 42;
	input.topology_id = 0x0102030405060708ULL;
	input.keyframe_frame_number = 42;
	input.vertex_count = 123;
	input.triangle_count = 234;
	input.payload = {0, 1, 2, 127, 255};

	const auto bytes = serialize_geometry_packet(input);
	REQUIRE(bytes.size() == kGeometryPacketHeaderSize + input.payload.size());

	GeometryPacket output;
	REQUIRE(parse_geometry_packet(bytes.data(), bytes.size(), output));
	CHECK(output.version == kGeometryPacketVersion);
	CHECK(output.flags == kGeometryPacketKeyframe);
	CHECK(output.frame_number == input.frame_number);
	CHECK(output.coding_mode == GeometryCodingMode::IndependentMesh);
	CHECK(output.payload_codec == GeometryPayloadCodec::DracoMesh);
	CHECK(output.topology_id == input.topology_id);
	CHECK(output.keyframe_frame_number == input.keyframe_frame_number);
	CHECK(output.vertex_count == input.vertex_count);
	CHECK(output.triangle_count == input.triangle_count);
	CHECK(output.payload == input.payload);
}

TEST_CASE("dependent geometry packets preserve their keyframe reference")
{
	GeometryPacket input;
	input.flags = 0;
	input.frame_number = 18;
	input.coding_mode = GeometryCodingMode::PositionUpdate;
	input.payload_codec = GeometryPayloadCodec::DracoPointCloud;
	input.topology_id = 99;
	input.keyframe_frame_number = 10;
	input.vertex_count = 1000;
	input.triangle_count = 1800;
	input.payload = {9, 8, 7};

	const auto bytes = serialize_geometry_packet(input);
	GeometryPacket output;
	REQUIRE(parse_geometry_packet(bytes.data(), bytes.size(), output));
	CHECK(output.coding_mode == GeometryCodingMode::PositionUpdate);
	CHECK(output.keyframe_frame_number == 10);
	CHECK(output.payload == input.payload);
}

TEST_CASE("geometry packet parser rejects malformed framing and dependencies")
{
	GeometryPacket invalid;
	CHECK_FALSE(parse_geometry_packet(nullptr, 0, invalid));

	GeometryPacket valid;
	valid.frame_number = 3;
	valid.topology_id = 1;
	valid.keyframe_frame_number = 3;
	valid.vertex_count = 3;
	valid.triangle_count = 1;
	valid.payload = {1, 2, 3};
	auto bytes = serialize_geometry_packet(valid);
	REQUIRE_FALSE(bytes.empty());

	SUBCASE("bad magic")
	{
		bytes[0] = 'X';
		CHECK_FALSE(parse_geometry_packet(bytes.data(), bytes.size(), invalid));
	}
	SUBCASE("unsupported version")
	{
		bytes[5] = 3;
		CHECK_FALSE(parse_geometry_packet(bytes.data(), bytes.size(), invalid));
	}
	SUBCASE("declared payload length does not match sample")
	{
		bytes[39] = 4;
		CHECK_FALSE(parse_geometry_packet(bytes.data(), bytes.size(), invalid));
	}
	SUBCASE("dependent frame cannot reference itself")
	{
		valid.flags = 0;
		valid.coding_mode = GeometryCodingMode::PositionUpdate;
		valid.payload_codec = GeometryPayloadCodec::DracoPointCloud;
		CHECK(serialize_geometry_packet(valid).empty());
	}
}

TEST_CASE("adaptive manifest parser validates the shared timeline")
{
	AdaptiveManifest manifest;
	std::string error;
	REQUIRE(AdaptiveManifestParser::parse(kValidManifest, manifest, error));
	CHECK(error.empty());
	CHECK(manifest.presentation_id == "test-presentation");
	CHECK(manifest.segments.size() == 2);
	CHECK(manifest.representations.size() == 2);
	CHECK(manifest.has_audio);
}

TEST_CASE("adaptive manifest parser rejects incomplete and discontinuous input")
{
	AdaptiveManifest manifest;
	std::string error;
	CHECK_FALSE(AdaptiveManifestParser::parse("{}", manifest, error));
	CHECK_FALSE(error.empty());

	REQUIRE(AdaptiveManifestParser::parse(kValidManifest, manifest, error));
	manifest.segments[1].start_seconds = 2.5;
	CHECK_FALSE(AdaptiveManifestParser::validate(manifest, error));
	CHECK(error.find("contiguous") != std::string::npos);

	REQUIRE(AdaptiveManifestParser::parse(kValidManifest, manifest, error));
	manifest.representations[1].id = manifest.representations[0].id;
	CHECK_FALSE(AdaptiveManifestParser::validate(manifest, error));
	CHECK(error.find("unique") != std::string::npos);
}

TEST_CASE("local adaptive selection honors quality and capability limits")
{
	AdaptiveSelection selection;
	std::string error;
	REQUIRE(select_adaptive_representation(
		kValidManifest,
		"/tmp/openvolumetric/manifest.json",
		AdaptiveQuality::Low,
		selection,
		error));
	CHECK(selection.representation.id == "low");
	CHECK(selection.resolved_resource == "/tmp/openvolumetric/low.mp4");

	REQUIRE(select_adaptive_representation(
		kValidManifest,
		"/tmp/openvolumetric/manifest.json",
		AdaptiveQuality::High,
		selection,
		error));
	CHECK(selection.representation.id == "high");

	AdaptiveCapabilityLimits limits;
	limits.maximum_texture_width = 512;
	limits.maximum_texture_height = 512;
	REQUIRE(select_adaptive_representation(
		kValidManifest,
		"/tmp/openvolumetric/manifest.json",
		AdaptiveQuality::Auto,
		limits,
		selection,
		error));
	CHECK(selection.representation.id == "low");
	CHECK(selection.capability_limited);
	CHECK(selection.eligible_representations.size() == 1);
}

TEST_CASE("adaptive policy moves through an arbitrary ladder one step at a time")
{
	AdaptivePolicy policy;
	policy.configure({
		{"high", "high.mp4", 4000000},
		{"low", "low.mp4", 1000000},
		{"medium", "medium.mp4", 2000000}});
	AdaptivePolicyObservation observation;
	observation.duration = 60.0;
	observation.segment_duration = 2.0;
	observation.presentation_time = 5.0;
	observation.input_state = AdaptivePolicyInputState::Ready;
	observation.active_representation = "low";
	observation.transfer_throughput_bps = 8000000;

	observation.now = 1.0;
	CHECK(policy.update(observation).action == AdaptivePolicyAction::Stay);
	observation.now = 11.1;
	const AdaptivePolicyDecision upgrade = policy.update(observation);
	CHECK(upgrade.action == AdaptivePolicyAction::Switch);
	CHECK(upgrade.target_representation == "medium");
	CHECK(upgrade.boundary_time == doctest::Approx(14.0));

	policy.reset();
	observation.active_representation = "high";
	observation.transfer_throughput_bps = 3000000;
	observation.now = 20.0;
	CHECK(policy.update(observation).action == AdaptivePolicyAction::Stay);
	observation.now = 22.1;
	const AdaptivePolicyDecision downgrade = policy.update(observation);
	CHECK(downgrade.action == AdaptivePolicyAction::Switch);
	CHECK(downgrade.target_representation == "medium");
}

TEST_CASE("adaptive policy handles rebuffering failures and manual override")
{
	AdaptivePolicy policy;
	policy.configure({
		{"low", "low.mp4", 1000000},
		{"high", "high.mp4", 4000000}});
	AdaptivePolicyObservation observation;
	observation.now = 5.0;
	observation.presentation_time = 4.0;
	observation.duration = 60.0;
	observation.segment_duration = 2.0;
	observation.input_state = AdaptivePolicyInputState::Rebuffering;
	observation.active_representation = "high";
	AdaptivePolicyDecision decision = policy.update(observation);
	CHECK(decision.action == AdaptivePolicyAction::Switch);
	CHECK(decision.target_representation == "low");

	observation.switch_state = AdaptivePolicySwitchState::Failed;
	observation.switch_generation = 7;
	decision = policy.update(observation);
	CHECK(decision.action == AdaptivePolicyAction::RetryLater);
	CHECK(decision.cancel_failed_switch);
	CHECK(decision.retry_time == doctest::Approx(10.0));
	decision = policy.update(observation);
	CHECK_FALSE(decision.cancel_failed_switch);

	observation.switch_state = AdaptivePolicySwitchState::Stable;
	observation.input_state = AdaptivePolicyInputState::Ready;
	observation.active_representation = "low";
	observation.now = 6.0;
	CHECK(policy.update(observation).action == AdaptivePolicyAction::RetryLater);
	const AdaptivePolicyDecision manual = policy.request(1, observation);
	CHECK(manual.action == AdaptivePolicyAction::Switch);
	CHECK(manual.target_representation == "high");
}

TEST_CASE("adaptive policy resets failure backoff after a committed switch")
{
	AdaptivePolicy policy;
	policy.configure({
		{"low", "low.mp4", 1000000},
		{"high", "high.mp4", 4000000}});
	AdaptivePolicyObservation observation;
	observation.now = 1.0;
	observation.duration = 60.0;
	observation.segment_duration = 2.0;
	observation.switch_state = AdaptivePolicySwitchState::Failed;
	observation.switch_generation = 1;
	policy.update(observation);
	CHECK(policy.retry_after() == doctest::Approx(6.0));

	observation.now = 2.0;
	observation.switch_state = AdaptivePolicySwitchState::Stable;
	observation.switch_count = 1;
	observation.active_representation = "low";
	CHECK(policy.update(observation).action == AdaptivePolicyAction::Stay);
	CHECK(policy.retry_after() < 0.0);
}

TEST_CASE("adaptive policy consumers make identical scripted decisions")
{
	const std::vector<AdaptivePolicyRepresentation> ladder = {
		{"low", "low.mp4", 1'000'000},
		{"medium", "medium.mp4", 2'000'000},
		{"high", "high.mp4", 4'000'000}};
	AdaptivePolicy unity_policy;
	AdaptivePolicy unreal_policy;
	unity_policy.configure(ladder);
	unreal_policy.configure(ladder);

	AdaptivePolicyObservation observation;
	observation.duration = 120.0;
	observation.segment_duration = 2.0;
	observation.input_state = AdaptivePolicyInputState::Ready;
	observation.switch_state = AdaptivePolicySwitchState::Stable;
	observation.active_representation = "medium";
	observation.transfer_throughput_bps = 1'500'000;

	for (const double now : {0.0, 1.0, 2.0, 3.0})
	{
		observation.now = now;
		observation.presentation_time = 20.0 + now;
		const auto unity_decision = unity_policy.update(observation);
		const auto unreal_decision = unreal_policy.update(observation);
		CHECK(unity_decision.action == unreal_decision.action);
		CHECK(unity_decision.target_index == unreal_decision.target_index);
		CHECK(unity_decision.boundary_time == unreal_decision.boundary_time);
		CHECK(unity_decision.retry_time == unreal_decision.retry_time);
		CHECK(unity_decision.cancel_failed_switch ==
			unreal_decision.cancel_failed_switch);
		CHECK(unity_decision.target_representation ==
			unreal_decision.target_representation);
	}
}

TEST_CASE("fragmented MP4 index produces bounded media ranges")
{
	const auto index = make_fragment_index();
	REQUIRE(index.size() == 70);
	const auto ranges = parse_fragmented_mp4_index(
		index.data(), index.size(), 1000, 1070);
	REQUIRE(ranges.size() == 2);
	CHECK(ranges[0].offset == 100);
	CHECK(ranges[0].size == 400);
	CHECK(ranges[1].offset == 500);
	CHECK(ranges[1].size == 500);
}

TEST_CASE("fragmented MP4 index rejects truncation and invalid offsets")
{
	auto index = make_fragment_index();
	CHECK(parse_fragmented_mp4_index(
		index.data(), index.size() - 1, 1000, 1069).empty());

	// Make the second fragment offset equal to the first. Offsets must increase.
	index[47] = 0;
	index[48] = 0;
	index[49] = 0;
	index[50] = 100;
	CHECK(parse_fragmented_mp4_index(
		index.data(), index.size(), 1000, 1070).empty());
}

TEST_CASE("local byte source reads, seeks, diagnoses, and cancels")
{
	const TemporaryFile file(".bin", "0123456789");
	LocalFileByteSource source(file.path());
	REQUIRE(source.is_open());
	CHECK(source.size() == 10);
	CHECK(source.is_seekable());

	std::uint8_t bytes[4]{};
	CHECK(source.read(bytes, sizeof(bytes)) == 4);
	CHECK(std::string(reinterpret_cast<char*>(bytes), 4) == "0123");
	CHECK(source.seek(-2, ByteSeekOrigin::Current) == 2);
	CHECK(source.read(bytes, 2) == 2);
	CHECK(std::string(reinterpret_cast<char*>(bytes), 2) == "23");
	CHECK(source.seek(-2, ByteSeekOrigin::End) == 8);
	CHECK(source.read(bytes, sizeof(bytes)) == 2);
	CHECK(std::string(reinterpret_cast<char*>(bytes), 2) == "89");
	CHECK(source.read(bytes, sizeof(bytes)) == 0);

	const ByteSourceDiagnostics ready = source.diagnostics();
	CHECK(ready.state == ByteSourceState::Ready);
	CHECK_FALSE(ready.remote);
	CHECK(ready.resource_size_bytes == 10);
	CHECK(source.seek(11, ByteSeekOrigin::Begin) == -1);
	CHECK_FALSE(source.error().empty());

	source.cancel();
	CHECK(source.is_cancelled());
	CHECK(source.read(bytes, sizeof(bytes)) == -1);
	CHECK(source.seek(0, ByteSeekOrigin::Begin) == -1);
	CHECK(source.diagnostics().state == ByteSourceState::Cancelled);
}

TEST_CASE("local byte source reports missing input without partial state")
{
	const std::filesystem::path missing =
		std::filesystem::temp_directory_path() /
		"openvolumetric-test-input-that-does-not-exist.bin";
	std::error_code ignored;
	std::filesystem::remove(missing, ignored);

	LocalFileByteSource source(missing);
	CHECK_FALSE(source.is_open());
	CHECK_FALSE(source.is_seekable());
	CHECK(source.size() == -1);
	CHECK(source.diagnostics().state == ByteSourceState::Error);
	CHECK_FALSE(source.error().empty());
}

TEST_CASE("player failure and shutdown lifecycle is idempotent")
{
	OpenVolumetricPlayer player;
	CHECK_FALSE(player.start());
	CHECK_FALSE(player.seek(0.0));
	CHECK_FALSE(player.seek(-1.0));
	CHECK_FALSE(player.open(nullptr));
	CHECK_FALSE(player.error().empty());

	player.stop();
	player.close();
	player.close();
	player.stop();
	CHECK(player.media_info().width == 0);
	CHECK(player.media_info().duration == 0.0);

	const TemporaryFile corrupt(".mp4", "not an MP4 container");
	CHECK_FALSE(player.open(corrupt.path().string().c_str()));
	CHECK_FALSE(player.error().empty());
	player.cancel_pending_io();
	player.close();
	player.close();
	CHECK_FALSE(player.start());
}

TEST_CASE("topology identity ignores positions but detects structural changes")
{
	const TemporaryFile first(
		".obj", triangle_obj("1 0 0", "1 0"));
	const TemporaryFile moved(
		".obj", triangle_obj("1.25 0.5 0", "1 0"));
	const TemporaryFile changed_uv(
		".obj", triangle_obj("1.25 0.5 0", "0.5 0.5"));
	const TemporaryFile changed_winding(
		".obj", triangle_obj("1.25 0.5 0", "1 0", true));

	authoring::CanonicalMesh first_mesh;
	authoring::CanonicalMesh moved_mesh;
	authoring::CanonicalMesh uv_mesh;
	authoring::CanonicalMesh winding_mesh;
	authoring::TopologyOptions options;
	std::string error;
	REQUIRE(authoring::load_canonical_obj(
		first.path(), options, first_mesh, error));
	REQUIRE(authoring::load_canonical_obj(
		moved.path(), options, moved_mesh, error));
	REQUIRE(authoring::load_canonical_obj(
		changed_uv.path(), options, uv_mesh, error));
	REQUIRE(authoring::load_canonical_obj(
		changed_winding.path(), options, winding_mesh, error));

	CHECK(authoring::topology_matches(first_mesh, moved_mesh));
	CHECK(first_mesh.topology_id == moved_mesh.topology_id);
	CHECK_FALSE(authoring::topology_matches(first_mesh, uv_mesh));
	CHECK_FALSE(authoring::topology_matches(first_mesh, winding_mesh));
}

TEST_CASE("player opens, presents, seeks, reaches the tail, and restarts")
{
	OpenVolumetricPlayer player;
	REQUIRE(player.open(OPENVOLUMETRIC_TEST_FIXTURE));
	const OpenVolumetricMediaInfo info = player.media_info();
	CHECK(info.width == 16);
	CHECK(info.height == 16);
	CHECK(info.frame_rate == doctest::Approx(4.0));
	CHECK(info.duration == doctest::Approx(1.0).epsilon(0.05));
	CHECK(info.has_audio);
	CHECK(info.audio_sample_rate == 48000);
	CHECK(info.audio_channels == 2);

	REQUIRE(player.start());
	CHECK(player.start());
	for (int frame = 0; frame < 4; ++frame)
	{
		OpenVolumetricPresentation presentation;
		const double requested = static_cast<double>(frame) / 4.0;
		CAPTURE(frame);
		CAPTURE(requested);
		CAPTURE(player.error());
		REQUIRE(wait_for_presentation(player, requested, presentation));
		CHECK(presentation.presentation_time ==
			doctest::Approx(requested).epsilon(0.01));
		CHECK(presentation.width == 16);
		CHECK(presentation.height == 16);
		CHECK(presentation.y.size() == 16 * 16);
		CHECK(presentation.u.size() == 8 * 8);
		CHECK(presentation.v.size() == 8 * 8);
		CHECK(presentation.mesh.verts.size() == 3);
		CHECK(presentation.mesh.indexes.size() == 3);
	}

	REQUIRE(player.seek(0.0));
	OpenVolumetricPresentation restarted;
	REQUIRE(wait_for_presentation(player, 0.0, restarted));
	CHECK(restarted.presentation_time == doctest::Approx(0.0).epsilon(0.01));

	player.stop();
	player.stop();
	REQUIRE(player.start());
	REQUIRE(player.seek(0.5));
	OpenVolumetricPresentation sought;
	REQUIRE(wait_for_presentation(player, 0.5, sought));
	CHECK(sought.presentation_time == doctest::Approx(0.5).epsilon(0.01));

	player.close();
	player.close();
	CHECK_FALSE(player.start());
}

TEST_CASE("player supports independent geometry and no-audio inputs")
{
	SUBCASE("independent geometry")
	{
		OpenVolumetricPlayer player;
		REQUIRE(player.open(OPENVOLUMETRIC_INDEPENDENT_FIXTURE));
		CHECK(player.media_info().has_audio);
		REQUIRE(player.start());
		OpenVolumetricPresentation presentation;
		REQUIRE(wait_for_presentation(player, 0.75, presentation));
		CHECK(presentation.mesh.verts.size() == 3);
	}

	SUBCASE("no audio")
	{
		OpenVolumetricPlayer player;
		REQUIRE(player.open(OPENVOLUMETRIC_NO_AUDIO_FIXTURE));
		CHECK_FALSE(player.media_info().has_audio);
		REQUIRE(player.start());
		OpenVolumetricPresentation presentation;
		REQUIRE(wait_for_presentation(player, 0.5, presentation));
		CHECK(presentation.mesh.indexes.size() == 3);
	}
}

TEST_CASE("fragmented fixture seeks through a temporal dependency")
{
	OpenVolumetricPlayer player;
	REQUIRE(player.open(OPENVOLUMETRIC_FRAGMENTED_FIXTURE));
	REQUIRE(player.start());
	REQUIRE(player.seek(0.75));
	OpenVolumetricPresentation presentation;
	REQUIRE(wait_for_presentation(player, 0.75, presentation));
	CHECK(presentation.presentation_time == doctest::Approx(0.75).epsilon(0.01));
	CHECK(presentation.mesh.verts.size() == 3);
	CHECK(presentation.mesh.indexes.size() == 3);
}

TEST_CASE("corrupt temporal geometry recovers at the next independent sample")
{
	OpenVolumetricPlayer player;
	REQUIRE(player.open(OPENVOLUMETRIC_CORRUPT_FIXTURE));
	REQUIRE(player.start());
	OpenVolumetricPresentation presentation;
	CHECK(wait_for_terminal_presentation_result(player, 0.25, presentation) ==
		FrameMatchResult::Missing);
	REQUIRE(wait_for_presentation(player, 0.5, presentation));
	CHECK(presentation.presentation_time == doctest::Approx(0.5).epsilon(0.01));
	CHECK(presentation.mesh.verts.size() == 3);
}

TEST_CASE("adaptive fixture resolves both representations into playable media")
{
	for (const AdaptiveQuality quality : {AdaptiveQuality::Low, AdaptiveQuality::High})
	{
		AdaptiveSelection selection;
		std::string error;
		REQUIRE(load_adaptive_representation(
			OPENVOLUMETRIC_ADAPTIVE_MANIFEST,
			quality,
			{},
			selection,
			error));
		CAPTURE(error);
		OpenVolumetricPlayer player;
		REQUIRE(player.open(selection.resolved_resource.c_str()));
		REQUIRE(player.start());
		OpenVolumetricPresentation presentation;
		REQUIRE(wait_for_presentation(player, 0.5, presentation));
		CHECK(presentation.mesh.verts.size() == 3);
	}
}
