#pragma once

#include "AdaptiveSelection.h"
#include "CoreMinimal.h"

/** Engine configuration required to resolve one playable MP4 resource. */
struct FOpenVolumetricSourceRequest final
{
	FString FilePath;
	FString Url;
	bool bUseAdaptiveManifest = false;
	openvolumetric::AdaptiveQuality AdaptiveQuality =
		openvolumetric::AdaptiveQuality::Auto;
	openvolumetric::AdaptiveCapabilityLimits CapabilityLimits;
};

/** One eligible adaptive representation retained for runtime switching. */
struct FOpenVolumetricResolvedRepresentation final
{
	FString Id;
	FString Resource;
	uint64 Bandwidth = 0;
};

/** Result of resolving local, HTTP, or manifest input without opening codecs. */
struct FOpenVolumetricResolvedSource final
{
	FString Resource;
	bool bRemote = false;
	FString RepresentationId;
	double MeasuredThroughputMbps = 0.0;
	FString DecisionReason;
	double SegmentDuration = 0.0;
	TArray<FOpenVolumetricResolvedRepresentation> Representations;
	FString Error;
};

/**
 * Validates an Unreal source and delegates adaptive selection to the core.
 * This helper has no component, playback, rendering, or audio ownership.
 */
class FOpenVolumetricSourceResolver final
{
public:
	static bool Resolve(
		const FOpenVolumetricSourceRequest& Request,
		FOpenVolumetricResolvedSource& Result);

private:
	static bool ResolveLocalPath(FString& Path);
};
