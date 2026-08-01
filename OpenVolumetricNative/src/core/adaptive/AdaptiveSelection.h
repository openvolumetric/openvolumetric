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

/** Parsed presentation and the coupled representation selected for opening. */
struct AdaptiveSelection
{
	AdaptiveManifest manifest;
	AdaptiveRepresentation representation;
	std::string resolved_resource;
};

/** Parses JSON, selects one representation, and resolves its resource URI. */
bool select_adaptive_representation(
	std::string_view manifest_json,
	std::string_view manifest_location,
	AdaptiveQuality quality,
	AdaptiveSelection& selection,
	std::string& error);

/** Loads a local or HTTP manifest and performs the same startup selection. */
bool load_adaptive_representation(
	std::string_view manifest_location,
	AdaptiveQuality quality,
	AdaptiveSelection& selection,
	std::string& error);

} // namespace openvolumetric
