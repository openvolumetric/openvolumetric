#pragma once

#include "Components/ActorComponent.h"
#include "OpenVolumetricComponent.generated.h"

class UDynamicMeshComponent;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USoundWaveProcedural;
class UTexture2D;

/** High-level lifecycle exposed to C++ and Blueprints. */
UENUM(BlueprintType)
enum class EOpenVolumetricPlaybackState : uint8
{
	Closed,
	Opening,
	Ready,
	Playing,
	Paused,
	Ended,
	Error
};

/** Observable state of the local or HTTP byte source. */
UENUM(BlueprintType)
enum class EOpenVolumetricInputState : uint8
{
	Opening,
	Ready,
	Rebuffering,
	Error,
	Cancelled,
	Ended
};

/** Startup representation choice when the input points to an adaptive manifest. */
UENUM(BlueprintType)
enum class EOpenVolumetricAdaptiveQuality : uint8
{
	Auto,
	Low,
	High
};

/**
 * Blueprint-facing owner of one OpenVolumetric playback instance.
 *
 * This initial component establishes the Unreal API and lifecycle. Decoder,
 * dynamic-mesh, texture, material, and audio resources will sit behind it.
 */
UCLASS(
	ClassGroup = (OpenVolumetric),
	BlueprintType,
	Blueprintable,
	meta = (BlueprintSpawnableComponent))
class OPENVOLUMETRICRUNTIME_API UOpenVolumetricComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UOpenVolumetricComponent();
	virtual ~UOpenVolumetricComponent() override;

	/** OpenVolumetric MP4 path. Relative paths are resolved beneath Content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Input")
	FFilePath SourceFile;

	/** Optional HTTP(S) MP4 URL. When set, this takes precedence over SourceFile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Input")
	FString SourceUrl;

	/** Interpret SourceFile or SourceUrl as an adaptive manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Input")
	bool bUseAdaptiveManifest = false;

	/** Manual quality or a conservative HTTP startup throughput decision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Input")
	EOpenVolumetricAdaptiveQuality AdaptiveQuality =
		EOpenVolumetricAdaptiveQuality::Auto;

	/** Auto texture dimension ceiling; zero uses the platform profile. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Input|Adaptive",
		meta = (ClampMin = "0"))
	int32 AdaptiveMaximumTextureDimension = 0;

	/** Auto texture bitrate ceiling in Mbps; zero uses the platform profile. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Input|Adaptive",
		meta = (ClampMin = "0.0"))
	double AdaptiveMaximumTextureBitrateMbps = 0.0;

	/** Auto geometry bitrate ceiling in Mbps; zero uses the platform profile. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Input|Adaptive",
		meta = (ClampMin = "0.0"))
	double AdaptiveMaximumGeometryBitrateMbps = 0.0;

	/** Auto aggregate bitrate ceiling in Mbps; zero uses the platform profile. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Input|Adaptive",
		meta = (ClampMin = "0.0"))
	double AdaptiveMaximumBandwidthMbps = 0.0;

	/** Permit Auto HTTP playback to change quality at aligned boundaries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Input|Adaptive")
	bool bEnableLiveAdaptiveSwitching = true;

	/** Record adaptive transport, buffering, and switching measurements as CSV. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Developer|Evaluation")
	bool bRecordAdaptiveMetrics = false;

	/** CSV filename written beneath the Unreal project's Saved directory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Developer|Evaluation")
	FString AdaptiveMetricsFileName = TEXT("openvolumetric-adaptive-metrics.csv");

	/** Time between adaptive CSV samples. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Developer|Evaluation",
		meta = (ClampMin = "0.05", UIMin = "0.05", UIMax = "5.0", Units = "s"))
	double AdaptiveMetricsIntervalSeconds = 0.25;

	/** Start playback automatically after a file opens successfully. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Playback")
	bool bPlayOnOpen = true;

	/** Seek to the beginning when the presentation reaches its duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Playback")
	bool bLoop = true;

	/** Converts OpenVolumetric metres to Unreal centimetres by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Geometry")
	double GeometryScale = 100.0;

	/** Runtime mesh created and updated by this player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVolumetric|Geometry")
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

	/** Unlit material with a Texture2D parameter named OpenVolumetricTexture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Texture")
	TObjectPtr<UMaterialInterface> TextureMaterial;

	/** Adds an offset to the decoded Y (luminance) channel before RGB conversion. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Texture",
		meta = (ClampMin = "-0.2", ClampMax = "0.2", UIMin = "-0.2", UIMax = "0.2"))
	float LuminanceCorrection = 0.0f;

	/** Adds an offset to the decoded U (blue projection) channel. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Texture",
		meta = (ClampMin = "-0.2", ClampMax = "0.2", UIMin = "-0.2", UIMax = "0.2"))
	float BlueProjectionCorrection = 0.0f;

	/** Adds an offset to the decoded V (red projection) channel. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Texture",
		meta = (ClampMin = "-0.2", ClampMax = "0.2", UIMin = "-0.2", UIMax = "0.2"))
	float RedProjectionCorrection = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVolumetric|Texture")
	TObjectPtr<UTexture2D> PresentationTexture;

	/** Procedural output for the audio track embedded in the OpenVolumetric MP4. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVolumetric|Audio")
	TObjectPtr<UAudioComponent> AudioComponent;

	/** Places the decoded audio at the centroid of each presented geometry frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Audio")
	bool bEnableGeometryCentroidSpatialAudio = true;

	/** Time used to smooth centroid motion without changing the audio clock. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Audio",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.25", Units = "s"))
	float SpatialAudioSmoothingSeconds = 0.05f;

	/** Enables the runtime status panel and keyboard playback shortcuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVolumetric|Developer")
	bool bEnableDeveloperControls = true;

	/** Number of seconds moved by the left and right arrow shortcuts. */
	UPROPERTY(
		EditAnywhere,
		BlueprintReadWrite,
		Category = "OpenVolumetric|Developer",
		meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "60.0", Units = "s"))
	double DeveloperSeekSeconds = 10.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	EOpenVolumetricPlaybackState PlaybackState = EOpenVolumetricPlaybackState::Closed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	double DurationSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	double CurrentTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	FString LastError;

	/** Representation identifier selected from the current adaptive manifest. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	FString SelectedRepresentationId;

	/** Bandwidth measured by the bounded HTTP Auto probe, in megabits/second. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	double AdaptiveMeasuredThroughputMbps = 0.0;

	/** Human-readable explanation of the adaptive startup decision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	FString AdaptiveDecisionReason;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	FString PendingRepresentationId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	int64 AdaptiveSwitchCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	bool bRemoteSource = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	EOpenVolumetricInputState InputState =
		EOpenVolumetricInputState::Opening;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 ResourceSizeBytes = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 CachedBytes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 DownloadedBytes = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 TransferThroughputBitsPerSecond = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 HttpRequestCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 NetworkRecoveryCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	bool bFragmentedInput = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 ActiveFragment = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 FragmentCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status|Buffer")
	int64 CachedFragmentCount = 0;

	/** Opens SourceUrl or SourceFile and prepares native and engine resources. */
	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	bool Open();

	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	void Play();

	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	void Pause();

	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	bool Seek(double TimeSeconds);

	/** Requests Low or High at the next aligned boundary for testing. */
	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	bool RequestAdaptiveHigh(bool bHigh);

	/** Stops playback and releases native and Unreal resources. */
	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	void Close();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	class FOpenVolumetricPlayerAdapter* Player = nullptr;
	class FOpenVolumetricPlaybackClock* PlaybackClock = nullptr;
	class FOpenVolumetricNetworkRecovery* NetworkRecovery = nullptr;
	class FOpenVolumetricPresentationUploader* PresentationUploader = nullptr;
	void CreateDynamicMeshComponent();
	void UpdatePresentationTexture(
		const TArray<FColor>& Pixels,
		int32 Width,
		int32 Height);
	void InitializeAudio();
	void PumpAudio();
	void ResetAudio();
	void UpdateDeveloperControls(float DeltaTime);
	void UpdateBufferDiagnostics();
	void UpdateAdaptivePolicy();
	void RecordAdaptiveMetrics();
	void CloseAdaptiveMetrics();
	bool HandleNetworkRecovery();

	double LastPresentationTime = -1.0;
	FVector SmoothedAudioCentroid = FVector::ZeroVector;
	bool bHasAudioCentroid = false;

	struct FAdaptiveRuntimeRepresentation
	{
		FString Id;
		FString Resource;
		uint64 Bandwidth = 0;
	};
	TArray<FAdaptiveRuntimeRepresentation> AdaptiveRepresentations;
	double AdaptiveSegmentDuration = 0.0;
	double AdaptiveSmoothedThroughputBps = 0.0;
	class FOpenVolumetricAdaptiveMetrics* AdaptiveMetrics = nullptr;
	double AdaptiveMetricFrameMilliseconds = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> ProceduralSoundWave;

	TArray<int16> AudioSamples;
	/** Reused CPU conversion output; capacity is retained between frames. */
	TArray<FColor> PresentationPixels;
	float SmoothedDeveloperDeltaTime = 0.0f;
	float DeveloperStatusUpdateCountdown = 0.0f;
	uint64 DeveloperMessageKey = 0;
	bool bDeveloperOverlayVisible = true;
};
