#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openvolumetric
{

/** Texture characteristics used for capability filtering and quality choice. */
struct AdaptiveTextureProfile
{
    std::string codec;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bitrate = 0;
};

/** Geometry characteristics used to reject incompatible representation mixes. */
struct AdaptiveGeometryProfile
{
    std::string codec;
    std::uint32_t position_quantization_bits = 0;
    std::uint32_t bitrate = 0;
    bool temporal_compression = false;
};

/** One coupled texture/geometry quality stored as an aligned fragmented MP4. */
struct AdaptiveRepresentation
{
    std::string id;
    std::string resource_uri;
    std::string compatibility_group;
    std::uint64_t bandwidth = 0;
    AdaptiveTextureProfile texture;
    AdaptiveGeometryProfile geometry;
};

/** One addressable interval shared by every representation in the manifest. */
struct AdaptiveSegment
{
    std::uint32_t number = 0;
    double start_seconds = 0.0;
    double duration_seconds = 0.0;
};

/** Engine-neutral description consumed by future selection and switching code. */
struct AdaptiveManifest
{
    static constexpr std::uint32_t supported_version = 1;

    std::uint32_t version = 0;
    std::string presentation_id;
    double duration_seconds = 0.0;
    double segment_duration_seconds = 0.0;
    bool has_audio = false;
    std::vector<AdaptiveSegment> segments;
    std::vector<AdaptiveRepresentation> representations;
};

/** Parses the transport syntax and enforces the adaptive compatibility contract. */
class AdaptiveManifestParser final
{
public:
    static bool parse(
        std::string_view json,
        AdaptiveManifest& manifest,
        std::string& error);

    static bool validate(
        const AdaptiveManifest& manifest,
        std::string& error);
};

} // namespace openvolumetric
