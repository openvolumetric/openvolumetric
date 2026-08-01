#include "AdaptiveSelection.h"

#include "HttpRangeByteSource.h"
#include "IByteSource.h"
#include "LocalFileByteSource.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <vector>

namespace openvolumetric
{
namespace
{

constexpr std::int64_t maximum_manifest_bytes = 4 * 1024 * 1024;

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

} // namespace

bool select_adaptive_representation(
	std::string_view manifest_json,
	std::string_view manifest_location,
	AdaptiveQuality quality,
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
	const auto selected = quality == AdaptiveQuality::Low
		? std::min_element(
			manifest.representations.begin(),
			manifest.representations.end(), ordering)
		: std::max_element(
			manifest.representations.begin(),
			manifest.representations.end(), ordering);
	AdaptiveSelection result;
	result.manifest = std::move(manifest);
	result.representation = *selected;
	if (!resolve_resource(
			manifest_location,
			result.representation.resource_uri,
			result.resolved_resource,
			error))
	{
		return false;
	}
	selection = std::move(result);
	return true;
}

bool load_adaptive_representation(
	std::string_view manifest_location,
	AdaptiveQuality quality,
	AdaptiveSelection& selection,
	std::string& error)
{
	std::string text;
	return read_manifest(manifest_location, text, error) &&
		select_adaptive_representation(
			text, manifest_location, quality, selection, error);
}

} // namespace openvolumetric
