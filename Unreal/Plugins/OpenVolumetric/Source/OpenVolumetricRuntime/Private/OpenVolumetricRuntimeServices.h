#pragma once

#include "CoreMinimal.h"

class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UObject;
class UTexture2D;

enum class EOpenVolumetricClockAction : uint8
{
	Continue,
	Loop,
	End
};

/** Pure playback-clock policy kept outside the reflected component. */
class FOpenVolumetricPlaybackClock final
{
public:
	EOpenVolumetricClockAction Advance(
		double DeltaSeconds,
		double DurationSeconds,
		bool bLoop,
		double& CurrentTimeSeconds) const;
};

enum class EOpenVolumetricRecoveryAction : uint8
{
	None,
	BeginRebuffer,
	Wait,
	Resume,
	Fail
};

/** Transport recovery state independent of decoder and audio ownership. */
class FOpenVolumetricNetworkRecovery final
{
public:
	EOpenVolumetricRecoveryAction Update(
		bool bRemote,
		int32 InputState,
		bool bPlaying,
		double PresentedTime,
		double CurrentTime);
	void Reset();
	double Target() const { return RecoveryTarget; }
	bool ShouldResume() const { return bResume; }

private:
	bool bRebuffering = false;
	bool bResume = false;
	double RecoveryTarget = 0.0;
};

/** Main-thread Unreal texture upload isolated from playback orchestration. */
struct FOpenVolumetricUploadMetrics
{
	uint64 SubmittedFrames = 0;
	uint64 DroppedFrames = 0;
	uint64 StorageGrowths = 0;
};

class FOpenVolumetricPresentationUploader final
{
public:
	FOpenVolumetricPresentationUploader();
	~FOpenVolumetricPresentationUploader();

	bool UpdateTexture(
		UObject* Owner,
		UDynamicMeshComponent* MeshComponent,
		UMaterialInterface* Material,
		const TArray<FColor>& Pixels,
		int32 Width,
		int32 Height,
		UTexture2D*& Texture,
		UMaterialInstanceDynamic*& DynamicMaterial);
	FOpenVolumetricUploadMetrics GetMetrics() const;

private:
	struct FUploadState;
	TSharedPtr<FUploadState, ESPMode::ThreadSafe> UploadState;
	FOpenVolumetricUploadMetrics Metrics;
};
