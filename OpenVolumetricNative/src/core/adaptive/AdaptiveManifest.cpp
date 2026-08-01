#include "AdaptiveManifest.h"

#include <cmath>
#include <exception>
#include <set>

#include <nlohmann/json.hpp>

namespace openvolumetric
{
namespace
{

using Json = nlohmann::json;

template <typename Value>
bool required(
    const Json& object,
    const char* name,
    Value& value,
    std::string& error)
{
    const auto member = object.find(name);
    if (member == object.end() || member->is_null())
    {
        error = std::string("Adaptive manifest is missing '") + name + "'.";
        return false;
    }
    try
    {
        value = member->get<Value>();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = std::string("Adaptive manifest field '") + name +
            "' has the wrong type: " + exception.what();
        return false;
    }
}

bool parse_representation(
    const Json& json,
    AdaptiveRepresentation& representation,
    std::string& error)
{
    if (!json.is_object())
    {
        error = "Each adaptive representation must be an object.";
        return false;
    }
    if (!required(json, "id", representation.id, error) ||
        !required(json, "resource_uri", representation.resource_uri, error) ||
        !required(
            json,
            "compatibility_group",
            representation.compatibility_group,
            error) ||
        !required(json, "bandwidth", representation.bandwidth, error))
    {
        return false;
    }

    const auto texture = json.find("texture");
    const auto geometry = json.find("geometry");
    if (texture == json.end() || !texture->is_object() ||
        geometry == json.end() || !geometry->is_object())
    {
        error = "Each representation requires texture and geometry objects.";
        return false;
    }
    return required(*texture, "codec", representation.texture.codec, error) &&
        required(*texture, "width", representation.texture.width, error) &&
        required(*texture, "height", representation.texture.height, error) &&
        required(*texture, "bitrate", representation.texture.bitrate, error) &&
        required(*geometry, "codec", representation.geometry.codec, error) &&
        required(
            *geometry,
            "position_quantization_bits",
            representation.geometry.position_quantization_bits,
            error) &&
        required(*geometry, "bitrate", representation.geometry.bitrate, error) &&
        required(
            *geometry,
            "temporal_compression",
            representation.geometry.temporal_compression,
            error);
}

bool parse_segment(
    const Json& json,
    AdaptiveSegment& segment,
    std::string& error)
{
    if (!json.is_object())
    {
        error = "Each adaptive segment must be an object.";
        return false;
    }
    return required(json, "number", segment.number, error) &&
        required(json, "start_seconds", segment.start_seconds, error) &&
        required(json, "duration_seconds", segment.duration_seconds, error);
}

} // namespace

bool AdaptiveManifestParser::parse(
    std::string_view text,
    AdaptiveManifest& manifest,
    std::string& error)
{
    AdaptiveManifest parsed;
    error.clear();
    try
    {
        const Json json = Json::parse(text.begin(), text.end());
        if (!json.is_object())
        {
            error = "Adaptive manifest root must be an object.";
            return false;
        }

        std::string format;
        if (!required(json, "format", format, error) ||
            format != "openvolumetric-adaptive")
        {
            if (error.empty())
            {
                error = "Adaptive manifest has an unsupported format.";
            }
            return false;
        }
        if (!required(json, "version", parsed.version, error) ||
            !required(json, "presentation_id", parsed.presentation_id, error) ||
            !required(json, "duration_seconds", parsed.duration_seconds, error) ||
            !required(
                json,
                "segment_duration_seconds",
                parsed.segment_duration_seconds,
                error) ||
            !required(json, "has_audio", parsed.has_audio, error))
        {
            return false;
        }

        const auto segments = json.find("segments");
        if (segments == json.end() || !segments->is_array())
        {
            error = "Adaptive manifest requires a segments array.";
            return false;
        }
        parsed.segments.reserve(segments->size());
        for (const Json& segment_json : *segments)
        {
            AdaptiveSegment segment;
            if (!parse_segment(segment_json, segment, error))
            {
                return false;
            }
            parsed.segments.push_back(segment);
        }

        const auto representations = json.find("representations");
        if (representations == json.end() || !representations->is_array())
        {
            error = "Adaptive manifest requires a representations array.";
            return false;
        }
        parsed.representations.reserve(representations->size());
        for (const Json& representation_json : *representations)
        {
            AdaptiveRepresentation representation;
            if (!parse_representation(
                    representation_json,
                    representation,
                    error))
            {
                return false;
            }
            parsed.representations.push_back(std::move(representation));
        }
    }
    catch (const std::exception& exception)
    {
        error = std::string("Failed to parse adaptive manifest: ") +
            exception.what();
        return false;
    }

    if (!validate(parsed, error))
    {
        return false;
    }
    manifest = std::move(parsed);
    return true;
}

bool AdaptiveManifestParser::validate(
    const AdaptiveManifest& manifest,
    std::string& error)
{
    error.clear();
    if (manifest.version != AdaptiveManifest::supported_version)
    {
        error = "Unsupported adaptive manifest version " +
            std::to_string(manifest.version) + ".";
        return false;
    }
    if (manifest.presentation_id.empty())
    {
        error = "Adaptive presentation_id cannot be empty.";
        return false;
    }
    if (!std::isfinite(manifest.duration_seconds) ||
        manifest.duration_seconds <= 0.0 ||
        !std::isfinite(manifest.segment_duration_seconds) ||
        manifest.segment_duration_seconds <= 0.0 ||
        manifest.segment_duration_seconds > manifest.duration_seconds)
    {
        error = "Adaptive presentation and segment durations must be valid.";
        return false;
    }
    if (manifest.representations.size() < 2)
    {
        error = "Adaptive manifests require at least two representations.";
        return false;
    }
    if (manifest.segments.empty())
    {
        error = "Adaptive manifests require a non-empty shared segment timeline.";
        return false;
    }
    double expected_start = 0.0;
    for (std::size_t index = 0; index < manifest.segments.size(); ++index)
    {
        const AdaptiveSegment& segment = manifest.segments[index];
        if (segment.number != index ||
            !std::isfinite(segment.start_seconds) ||
            !std::isfinite(segment.duration_seconds) ||
            segment.duration_seconds <= 0.0 ||
            std::abs(segment.start_seconds - expected_start) > 0.000001)
        {
            error =
                "Adaptive segment timeline must be contiguous and numbered from zero.";
            return false;
        }
        expected_start += segment.duration_seconds;
    }
    if (std::abs(expected_start - manifest.duration_seconds) > 0.000001)
    {
        error =
            "Adaptive segment timeline does not cover the presentation duration.";
        return false;
    }

    std::set<std::string> identifiers;
    for (const AdaptiveRepresentation& representation : manifest.representations)
    {
        if (representation.id.empty() ||
            !identifiers.insert(representation.id).second)
        {
            error = "Adaptive representation identifiers must be non-empty and unique.";
            return false;
        }
        if (representation.resource_uri.empty() ||
            representation.compatibility_group.empty() ||
            representation.bandwidth == 0 ||
            representation.texture.codec.empty() ||
            representation.texture.width == 0 ||
            representation.texture.height == 0 ||
            representation.texture.bitrate == 0 ||
            representation.geometry.codec.empty() ||
            representation.geometry.bitrate == 0 ||
            representation.geometry.position_quantization_bits == 0)
        {
            error = "Adaptive representation '" + representation.id +
                "' contains an empty or zero required field.";
            return false;
        }
        if (representation.bandwidth <
            static_cast<std::uint64_t>(representation.texture.bitrate) +
                representation.geometry.bitrate)
        {
            error = "Adaptive representation '" + representation.id +
                "' bandwidth is lower than its declared media bitrates.";
            return false;
        }
    }
    return true;
}

} // namespace openvolumetric
