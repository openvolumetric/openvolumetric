#include "AdaptiveSelection.h"

#include "HttpRangeByteSource.h"
#include "IByteSource.h"
#include "LocalFileByteSource.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace openvolumetric
{
namespace
{

constexpr std::int64_t maximum_manifest_bytes = 4 * 1024 * 1024;
constexpr std::size_t throughput_probe_bytes = 2 * 1024 * 1024;
constexpr double throughput_safety_factor = 1.5;

std::uint64_t probe_http_throughput(std::string_view url)
{
	HttpRangeByteSourceOptions options;
	options.block_size = 512 * 1024;
	options.maximum_cache_bytes = 4 * 1024 * 1024;
	options.sequential_read_ahead_blocks = 0;
	options.maximum_retry_count = 2;
	options.request_timeout_ms = 5000;
	const auto started = std::chrono::steady_clock::now();
	HttpRangeByteSource source(std::string(url), options);
	if (!source.is_open())
		return 0;
	const std::int64_t available = source.size();
	if (available <= 0)
		return 0;
	const std::size_t target = static_cast<std::size_t>(std::min<std::int64_t>(
		available, static_cast<std::int64_t>(throughput_probe_bytes)));
	std::vector<std::uint8_t> buffer(256 * 1024);
	std::size_t received = 0;
	while (received < target)
	{
		const std::size_t requested = std::min(buffer.size(), target - received);
		const std::int64_t count = source.read(buffer.data(), requested);
		if (count <= 0)
			return 0;
		received += static_cast<std::size_t>(count);
	}
	const double seconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now() - started).count();
	if (seconds <= 0.0)
		return 0;
	return static_cast<std::uint64_t>(
		static_cast<double>(received) * 8.0 / seconds);
}

bool resolve_resource(
	std::string_view manifest_location,
	std::string_view resource_uri,
	std::string& output,
	std::string& error)
{
	if (manifest_location.empty() || resource_uri.empty())
	{
		error = "Adaptive manifest and resource locations cannot be empty.";
		return false;
	}
	if (is_http_url(resource_uri))
	{
		output.assign(resource_uri);
		return true;
	}
	if (is_http_url(manifest_location))
	{
		std::string base(manifest_location);
		const std::size_t suffix = base.find_first_of("?#");
		if (suffix != std::string::npos)
			base.erase(suffix);
		const std::size_t scheme = base.find("://");
		const std::size_t authority_end = scheme == std::string::npos
			? std::string::npos
			: base.find('/', scheme + 3);
		if (!resource_uri.empty() && resource_uri.front() == '/')
		{
			output = authority_end == std::string::npos
				? base + std::string(resource_uri)
				: base.substr(0, authority_end) + std::string(resource_uri);
			return true;
		}
		const std::size_t slash = base.rfind('/');
		if (slash == std::string::npos || slash < scheme + 2)
		{
			error = "Adaptive manifest URL has no resolvable parent path.";
			return false;
		}
		output = base.substr(0, slash + 1) + std::string(resource_uri);
		return true;
	}
	output = (std::filesystem::path(manifest_location).parent_path() /
		std::filesystem::path(resource_uri)).lexically_normal().string();
	return true;
}

bool read_manifest(
	std::string_view location,
	std::string& text,
	std::string& error)
{
	std::unique_ptr<IByteSource> source;
	if (is_http_url(location))
	{
		auto remote = std::make_unique<HttpRangeByteSource>(std::string(location));
		if (!remote->is_open())
		{
			error = remote->error();
			return false;
		}
		source = std::move(remote);
	}
	else
	{
		auto local = std::make_unique<LocalFileByteSource>(
			std::filesystem::path(location));
		if (!local->is_open())
		{
			error = local->error();
			return false;
		}
		source = std::move(local);
	}
	const std::int64_t size = source->size();
	if (size <= 0 || size > maximum_manifest_bytes)
	{
		error = "Adaptive manifest size is empty, unknown, or exceeds 4 MiB.";
		return false;
	}
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
	std::size_t total = 0;
	while (total < bytes.size())
	{
		const std::int64_t count = source->read(
			bytes.data() + total, bytes.size() - total);
		if (count <= 0)
		{
			error = source->error().empty()
				? "Adaptive manifest ended before its declared size."
				: source->error();
			return false;
		}
		total += static_cast<std::size_t>(count);
	}
	text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	return true;
}

bool within_limit(std::uint64_t value, std::uint64_t maximum)
{
	return maximum == 0 || value <= maximum;
}

bool supports_representation(
	const AdaptiveRepresentation& representation,
	const AdaptiveCapabilityLimits& limits)
{
	return within_limit(
			representation.texture.width, limits.maximum_texture_width) &&
		within_limit(
			representation.texture.height, limits.maximum_texture_height) &&
		within_limit(
			representation.texture.bitrate, limits.maximum_texture_bitrate) &&
		within_limit(
			representation.geometry.bitrate, limits.maximum_geometry_bitrate) &&
		within_limit(representation.bandwidth, limits.maximum_bandwidth);
}

} // namespace

bool select_adaptive_representation(
	std::string_view manifest_json,
	std::string_view manifest_location,
	AdaptiveQuality quality,
	AdaptiveSelection& selection,
	std::string& error)
{
	return select_adaptive_representation(
		manifest_json,
		manifest_location,
		quality,
		{},
		selection,
		error);
}

bool select_adaptive_representation(
	std::string_view manifest_json,
	std::string_view manifest_location,
	AdaptiveQuality quality,
	const AdaptiveCapabilityLimits& limits,
	AdaptiveSelection& selection,
	std::string& error)
{
	AdaptiveManifest manifest;
	if (!AdaptiveManifestParser::parse(manifest_json, manifest, error))
		return false;
	const auto ordering = [](const auto& left, const auto& right)
	{
		return left.bandwidth < right.bandwidth;
	};
	auto eligible = manifest.representations;
	bool capability_limited = false;
	if (quality == AdaptiveQuality::Auto)
	{
		eligible.erase(
			std::remove_if(
				eligible.begin(),
				eligible.end(),
				[&limits](const AdaptiveRepresentation& representation)
				{
					return !supports_representation(representation, limits);
				}),
			eligible.end());
		capability_limited = eligible.size() != manifest.representations.size();
		if (eligible.empty())
		{
			error = "No adaptive representation satisfies the device capability limits.";
			return false;
		}
	}
	const auto& candidates = quality == AdaptiveQuality::Auto
		? eligible
		: manifest.representations;
	const auto selected = quality == AdaptiveQuality::Low
		? std::min_element(candidates.begin(), candidates.end(), ordering)
		: std::max_element(candidates.begin(), candidates.end(), ordering);
	AdaptiveSelection result;
	result.manifest = std::move(manifest);
	result.representation = *selected;
	result.capability_limited = capability_limited;
	if (!resolve_resource(
			manifest_location,
			result.representation.resource_uri,
			result.resolved_resource,
			error))
	{
		return false;
	}
	selection = std::move(result);
	selection.decision_reason = quality == AdaptiveQuality::Low
		? "Manual Low selected the minimum-bandwidth representation."
		: quality == AdaptiveQuality::High
			? "Manual High selected the maximum-bandwidth representation."
			: capability_limited
				? "Local Auto selected the highest device-compatible representation."
				: "Local Auto selected the maximum-bandwidth representation.";
	return true;
}

bool load_adaptive_representation(
	std::string_view manifest_location,
	AdaptiveQuality quality,
	AdaptiveSelection& selection,
	std::string& error)
{
	return load_adaptive_representation(
		manifest_location, quality, {}, selection, error);
}

bool load_adaptive_representation(
	std::string_view manifest_location,
	AdaptiveQuality quality,
	const AdaptiveCapabilityLimits& limits,
	AdaptiveSelection& selection,
	std::string& error)
{
	std::string text;
	if (!read_manifest(manifest_location, text, error) ||
		!select_adaptive_representation(
			text, manifest_location, quality, limits, selection, error))
	{
		return false;
	}
	if (quality != AdaptiveQuality::Auto || !is_http_url(manifest_location))
		return true;

	selection.measured_throughput_bps =
		probe_http_throughput(selection.resolved_resource);
	const double required = static_cast<double>(selection.representation.bandwidth) *
		throughput_safety_factor;
	if (selection.measured_throughput_bps > 0 &&
		static_cast<double>(selection.measured_throughput_bps) >= required)
	{
		selection.decision_reason = selection.capability_limited
			? "HTTP Auto selected the highest device-compatible representation with 1.5x bandwidth headroom."
			: "HTTP Auto selected High with 1.5x measured bandwidth headroom.";
		return true;
	}

	const std::uint64_t measured = selection.measured_throughput_bps;
	const auto lowest = std::min_element(
		selection.manifest.representations.begin(),
		selection.manifest.representations.end(),
		[&limits](
			const AdaptiveRepresentation& left,
			const AdaptiveRepresentation& right)
		{
			const bool left_supported = supports_representation(left, limits);
			const bool right_supported = supports_representation(right, limits);
			if (left_supported != right_supported)
				return left_supported;
			return left.bandwidth < right.bandwidth;
		});
	selection.representation = *lowest;
	if (!resolve_resource(
			manifest_location,
			selection.representation.resource_uri,
			selection.resolved_resource,
			error))
	{
		return false;
	}
	selection.measured_throughput_bps = measured;
	selection.decision_reason = measured == 0
		? "HTTP Auto selected Low because the bounded throughput probe failed."
		: "HTTP Auto selected Low because High lacked 1.5x bandwidth headroom.";
	return true;
}

} // namespace openvolumetric
