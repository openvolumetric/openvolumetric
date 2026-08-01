#pragma once

#include "AdaptiveManifest.h"

#include <string>
#include <string_view>

namespace openvolumetric
{

/** Startup-only quality choice used before dynamic adaptation is implemented. */
enum class AdaptiveQuality
{
	Auto = 0,
	Low = 1,
	High = 2
};

/** Zero-valued fields are unlimited; non-zero fields constrain Auto selection. */
struct AdaptiveCapabilityLimits
{
	std::uint32_t maximum_texture_width = 0;
	std::uint32_t maximum_texture_height = 0;
	std::uint64_t maximum_texture_bitrate = 0;
	std::uint64_t maximum_geometry_bitrate = 0;
	std::uint64_t maximum_bandwidth = 0;
};

/** Parsed presentation and the coupled representation selected for opening. */
struct AdaptiveSelection
{
	AdaptiveManifest manifest;
	AdaptiveRepresentation representation;
	std::string resolved_resource;
	std::uint64_t measured_throughput_bps = 0;
	bool capability_limited = false;
	std::string decision_reason;
};

/** Parses JSON, selects one representation, and resolves its resource URI. */
bool select_adaptive_representation(
	std::string_view manifest_json,
	std::string_view manifest_location,
	AdaptiveQuality quality,
	AdaptiveSelection& selection,
	std::string& error);

/** Selects with platform limits when quality is Auto. */
bool select_adaptive_representation(
	std::string_view manifest_json,
	std::string_view manifest_location,
	AdaptiveQuality quality,
	const AdaptiveCapabilityLimits& limits,
	AdaptiveSelection& selection,
	std::string& error);

/** Loads a local or HTTP manifest and performs the same startup selection. */
bool load_adaptive_representation(
	std::string_view manifest_location,
	AdaptiveQuality quality,
	AdaptiveSelection& selection,
	std::string& error);

/** Loads and selects with platform limits when quality is Auto. */
bool load_adaptive_representation(
	std::string_view manifest_location,
	AdaptiveQuality quality,
	const AdaptiveCapabilityLimits& limits,
	AdaptiveSelection& selection,
	std::string& error);

} // namespace openvolumetric
