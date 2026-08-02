#include "OpenVolumetricComponent.h"

#include "AdaptiveSelection.h"
#include "OpenVolumetricPlayerAdapter.h"
#include "Components/AudioComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenVolumetricComponent, Log, All);

namespace
{

std::uint64_t ResolveBitrateLimit(double OverrideMbps, std::uint64_t PlatformLimit)
{
	return OverrideMbps > 0.0
		? static_cast<std::uint64_t>(OverrideMbps * 1000000.0)
		: PlatformLimit;
}

openvolumetric::AdaptiveCapabilityLimits GetAdaptiveCapabilityLimits(
	const UOpenVolumetricComponent& Component)
{
	openvolumetric::AdaptiveCapabilityLimits Limits;
#if PLATFORM_ANDROID
	constexpr std::uint32_t PlatformDimension = 4096;
	constexpr std::uint64_t PlatformTextureBitrate = 20000000;
	constexpr std::uint64_t PlatformGeometryBitrate = 50000000;
	constexpr std::uint64_t PlatformBandwidth = 70000000;
#else
	constexpr std::uint32_t PlatformDimension = 8192;
	constexpr std::uint64_t PlatformTextureBitrate = 100000000;
	constexpr std::uint64_t PlatformGeometryBitrate = 250000000;
	constexpr std::uint64_t PlatformBandwidth = 350000000;
#endif
	const std::uint32_t MaximumDimension =
		Component.AdaptiveMaximumTextureDimension > 0
			? static_cast<std::uint32_t>(Component.AdaptiveMaximumTextureDimension)
			: PlatformDimension;
	Limits.maximum_texture_width = MaximumDimension;
	Limits.maximum_texture_height = MaximumDimension;
	Limits.maximum_texture_bitrate = ResolveBitrateLimit(
		Component.AdaptiveMaximumTextureBitrateMbps, PlatformTextureBitrate);
	Limits.maximum_geometry_bitrate = ResolveBitrateLimit(
		Component.AdaptiveMaximumGeometryBitrateMbps, PlatformGeometryBitrate);
	Limits.maximum_bandwidth = ResolveBitrateLimit(
		Component.AdaptiveMaximumBandwidthMbps, PlatformBandwidth);
	return Limits;
}

FString Csv(const FString& Value)
{
	return TEXT("\"") + Value.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
}

void WriteUtf8Line(FArchive& Archive, const FString& Line)
{
	FTCHARToUTF8 Utf8(*(Line + LINE_TERMINATOR));
	Archive.Serialize(
		const_cast<ANSICHAR*>(Utf8.Get()),
		Utf8.Length());
}

} // namespace

UOpenVolumetricComponent::UOpenVolumetricComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	Player = new FOpenVolumetricPlayerAdapter();

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/OpenVolumetric/Materials/M_OpenVolumetricTexture.M_OpenVolumetricTexture"));
	if (Material.Succeeded())
	{
		TextureMaterial = Material.Object;
	}
}

UOpenVolumetricComponent::~UOpenVolumetricComponent()
{
	CloseAdaptiveMetrics();
	delete Player;
	Player = nullptr;
}

void UOpenVolumetricComponent::BeginPlay()
{
	Super::BeginPlay();
	CreateDynamicMeshComponent();
	DeveloperMessageKey = reinterpret_cast<uint64>(this);
	if (!SourceUrl.TrimStartAndEnd().IsEmpty() ||
		!SourceFile.FilePath.IsEmpty())
	{
		Open();
	}
}

void UOpenVolumetricComponent::CreateDynamicMeshComponent()
{
	if (DynamicMeshComponent != nullptr || GetOwner() == nullptr)
	{
		return;
	}

	DynamicMeshComponent = NewObject<UDynamicMeshComponent>(
		GetOwner(), TEXT("OpenVolumetricDynamicMesh"));
	GetOwner()->AddInstanceComponent(DynamicMeshComponent);
	if (USceneComponent* Root = GetOwner()->GetRootComponent())
	{
		DynamicMeshComponent->SetupAttachment(Root);
	}
	DynamicMeshComponent->SetCollisionEnabled(
		ECollisionEnabled::NoCollision);
	DynamicMeshComponent->SetCastShadow(false);
	// The video texture is routed through an unlit emissive material so its
	// captured colour is not relit by the host scene. It is display content,
	// not an area light: exclude it from Lumen's dynamic indirect lighting to
	// prevent bright frames from illuminating the surrounding level.
	DynamicMeshComponent->SetEmissiveLightSource(false);
	DynamicMeshComponent->SetAffectDynamicIndirectLighting(false);
	DynamicMeshComponent->RegisterComponent();
}

void UOpenVolumetricComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdaptiveMetricFrameMilliseconds = static_cast<double>(DeltaTime) * 1000.0;
	UpdateDeveloperControls(DeltaTime);
	UpdateBufferDiagnostics();
	RecordAdaptiveMetrics();
	if (HandleNetworkRecovery())
	{
		return;
	}
	UpdateAdaptivePolicy();
	if (PlaybackState != EOpenVolumetricPlaybackState::Playing ||
		Player == nullptr ||
		DynamicMeshComponent == nullptr)
	{
		return;
	}
	CurrentTimeSeconds += static_cast<double>(DeltaTime);
	// Feeding Unreal also drains the native PCM ring so audio backpressure
	// cannot stall the shared video/geometry demuxer.
	PumpAudio();
	if (DurationSeconds > 0.0 &&
		CurrentTimeSeconds >= DurationSeconds)
	{
		if (bLoop)
		{
			if (!Player->Seek(0.0, LastError))
			{
				PlaybackState = EOpenVolumetricPlaybackState::Error;
				return;
			}
			CurrentTimeSeconds = 0.0;
			ResetAudio();
		}
		else
		{
			PlaybackState = EOpenVolumetricPlaybackState::Ended;
			return;
		}
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	FVector GeometryCentroid = FVector::ZeroVector;
	TArray<FColor> Pixels;
	int32 TextureWidth = 0;
	int32 TextureHeight = 0;
	double PresentationTime = 0.0;
	const openvolumetric::FrameMatchResult Result =
		Player->PollPresentation(
			CurrentTimeSeconds,
			GeometryScale,
			LuminanceCorrection,
			BlueProjectionCorrection,
			RedProjectionCorrection,
			Mesh,
			GeometryCentroid,
			Pixels,
			TextureWidth,
			TextureHeight,
			PresentationTime);
	if (Result == openvolumetric::FrameMatchResult::Ready)
	{
		LastPresentationTime = PresentationTime;
		DynamicMeshComponent->SetMesh(MoveTemp(Mesh));
		if (AudioComponent != nullptr)
		{
			AudioComponent->bAllowSpatialization =
				bEnableGeometryCentroidSpatialAudio;
			if (bEnableGeometryCentroidSpatialAudio)
			{
				if (!bHasAudioCentroid || SpatialAudioSmoothingSeconds <= 0.0f)
				{
					SmoothedAudioCentroid = GeometryCentroid;
					bHasAudioCentroid = true;
				}
				else
				{
					// Exponential smoothing is independent of the game frame rate and
					// changes only source position, never decoded PCM timing.
					const float Blend = 1.0f - FMath::Exp(
						-DeltaTime / SpatialAudioSmoothingSeconds);
					SmoothedAudioCentroid = FMath::Lerp(
						SmoothedAudioCentroid, GeometryCentroid, Blend);
				}
				AudioComponent->SetRelativeLocation(SmoothedAudioCentroid);
			}
			else
			{
				AudioComponent->SetRelativeLocation(FVector::ZeroVector);
				bHasAudioCentroid = false;
			}
		}
		UpdatePresentationTexture(
			Pixels, TextureWidth, TextureHeight);
	}
}

void UOpenVolumetricComponent::UpdatePresentationTexture(
	const TArray<FColor>& Pixels,
	int32 Width,
	int32 Height)
{
	if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
	{
		return;
	}

	if (PresentationTexture == nullptr ||
		PresentationTexture->GetSizeX() != Width ||
		PresentationTexture->GetSizeY() != Height)
	{
		PresentationTexture = UTexture2D::CreateTransient(
			Width, Height, PF_B8G8R8A8);
		PresentationTexture->SRGB = true;
		PresentationTexture->Filter = TF_Bilinear;
		PresentationTexture->UpdateResource();

		if (TextureMaterial != nullptr)
		{
			DynamicMaterial = UMaterialInstanceDynamic::Create(
				TextureMaterial, this);
			DynamicMaterial->SetTextureParameterValue(
				TEXT("OpenVolumetricTexture"), PresentationTexture);
			DynamicMeshComponent->SetMaterial(0, DynamicMaterial);
		}
	}

	const SIZE_T ByteCount =
		static_cast<SIZE_T>(Pixels.Num()) * sizeof(FColor);
	uint8* Upload = static_cast<uint8*>(FMemory::Malloc(ByteCount));
	FMemory::Memcpy(Upload, Pixels.GetData(), ByteCount);
	FUpdateTextureRegion2D* Region =
		new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
	PresentationTexture->UpdateTextureRegions(
		0,
		1,
		Region,
		Width * sizeof(FColor),
		sizeof(FColor),
		Upload,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
}

void UOpenVolumetricComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GEngine != nullptr && DeveloperMessageKey != 0)
	{
		GEngine->RemoveOnScreenDebugMessage(DeveloperMessageKey);
	}
	Close();
	Super::EndPlay(EndPlayReason);
}

bool UOpenVolumetricComponent::Open()
{
	Close();

	const FString TrimmedUrl = SourceUrl.TrimStartAndEnd();
	const bool bUseRemoteSource = !TrimmedUrl.IsEmpty();
	if (!bUseRemoteSource && SourceFile.FilePath.IsEmpty())
	{
		LastError = TEXT("No OpenVolumetric input has been selected.");
		PlaybackState = EOpenVolumetricPlaybackState::Error;
		UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
		return false;
	}

	FString ResolvedPath = bUseRemoteSource
		? TrimmedUrl
		: SourceFile.FilePath;
	if (bUseRemoteSource)
	{
		if (!ResolvedPath.StartsWith(
				TEXT("http://"), ESearchCase::IgnoreCase) &&
			!ResolvedPath.StartsWith(
				TEXT("https://"), ESearchCase::IgnoreCase))
		{
			LastError = TEXT("SourceUrl must be an HTTP or HTTPS URL.");
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
			return false;
		}
	}
	else
	{
		FPaths::NormalizeFilename(ResolvedPath);
		if (FPaths::IsRelative(ResolvedPath))
		{
			// FFilePath may serialize a picker result relative to the editor,
			// project, or Content directory.
			const TArray<FString> Candidates = {
				FPaths::ConvertRelativePathToFull(ResolvedPath),
				FPaths::ConvertRelativePathToFull(
					FPaths::ProjectDir(), ResolvedPath),
				FPaths::ConvertRelativePathToFull(
					FPaths::ProjectContentDir(), ResolvedPath),
				FPaths::ConvertRelativePathToFull(
					FPaths::ProjectContentDir(),
					FPaths::GetCleanFilename(ResolvedPath))
			};
			for (const FString& Candidate : Candidates)
			{
				if (FPaths::FileExists(Candidate))
				{
					ResolvedPath = Candidate;
					break;
				}
			}
		}
		FPaths::NormalizeFilename(ResolvedPath);

		if (!FPaths::FileExists(ResolvedPath))
		{
			LastError = FString::Printf(
				TEXT("OpenVolumetric input does not exist: %s"), *ResolvedPath);
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
			return false;
		}
	}

	SelectedRepresentationId.Reset();
	AdaptiveMeasuredThroughputMbps = 0.0;
	AdaptiveDecisionReason.Reset();
	PendingRepresentationId.Reset();
	AdaptiveSwitchCount = 0;
	AdaptiveRepresentations.Reset();
	AdaptiveSegmentDuration = 0.0;
	if (bUseAdaptiveManifest)
	{
		openvolumetric::AdaptiveSelection Selection;
		std::string NativeError;
		const FTCHARToUTF8 Utf8Manifest(*ResolvedPath);
		if (!openvolumetric::load_adaptive_representation(
				Utf8Manifest.Get(),
				static_cast<openvolumetric::AdaptiveQuality>(AdaptiveQuality),
				GetAdaptiveCapabilityLimits(*this),
				Selection,
				NativeError))
		{
			LastError = UTF8_TO_TCHAR(NativeError.c_str());
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
			return false;
		}
		ResolvedPath = UTF8_TO_TCHAR(Selection.resolved_resource.c_str());
		SelectedRepresentationId = UTF8_TO_TCHAR(
			Selection.representation.id.c_str());
		AdaptiveMeasuredThroughputMbps =
			static_cast<double>(Selection.measured_throughput_bps) / 1000000.0;
		AdaptiveDecisionReason = UTF8_TO_TCHAR(Selection.decision_reason.c_str());
		AdaptiveSegmentDuration = Selection.manifest.segment_duration_seconds;
		for (const openvolumetric::ResolvedAdaptiveRepresentation& Entry :
			Selection.eligible_representations)
		{
			FAdaptiveRuntimeRepresentation RuntimeEntry;
			RuntimeEntry.Id = UTF8_TO_TCHAR(Entry.representation.id.c_str());
			RuntimeEntry.Resource = UTF8_TO_TCHAR(Entry.resolved_resource.c_str());
			RuntimeEntry.Bandwidth = Entry.representation.bandwidth;
			AdaptiveRepresentations.Add(MoveTemp(RuntimeEntry));
		}
		if (!bUseRemoteSource && !FPaths::FileExists(ResolvedPath))
		{
			LastError = FString::Printf(
				TEXT("Adaptive representation does not exist: %s"), *ResolvedPath);
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
			return false;
		}
		UE_LOG(
			LogOpenVolumetricComponent,
			Log,
			TEXT("Selected adaptive representation '%s': %s"),
			*SelectedRepresentationId,
			*ResolvedPath);
	}

	PlaybackState = EOpenVolumetricPlaybackState::Opening;
	if (!Player->Open(ResolvedPath, LastError))
	{
		PlaybackState = EOpenVolumetricPlaybackState::Error;
		UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
		return false;
	}
	if (bUseAdaptiveManifest)
	{
		Player->SetActiveRepresentationId(SelectedRepresentationId);
		Player->ClearAdaptivePolicy();
		for (const FAdaptiveRuntimeRepresentation& Representation :
			AdaptiveRepresentations)
		{
			Player->AddAdaptivePolicyRepresentation(
				Representation.Id,
				Representation.Resource,
				Representation.Bandwidth);
		}
	}

	const openvolumetric::OpenVolumetricMediaInfo& Info = Player->GetMediaInfo();
	DurationSeconds = Info.duration;
	CurrentTimeSeconds = 0.0;
	PlaybackState = EOpenVolumetricPlaybackState::Ready;
	LastError.Reset();
	UpdateBufferDiagnostics();
	InitializeAudio();

	UE_LOG(
		LogOpenVolumetricComponent,
		Log,
		TEXT("Opened OpenVolumetric MP4: %dx%d, %.3f fps, %.3f seconds, audio=%s"),
		Info.width,
		Info.height,
		Info.frame_rate,
		Info.duration,
		Info.has_audio ? TEXT("yes") : TEXT("no"));

	if (bPlayOnOpen)
	{
		Play();
	}
	return true;
}

void UOpenVolumetricComponent::Play()
{
	if (PlaybackState == EOpenVolumetricPlaybackState::Ended)
	{
		if (!Player->Seek(0.0, LastError))
		{
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			return;
		}
		CurrentTimeSeconds = 0.0;
		PlaybackState = EOpenVolumetricPlaybackState::Ready;
		ResetAudio();
	}
	if (PlaybackState == EOpenVolumetricPlaybackState::Ready ||
		PlaybackState == EOpenVolumetricPlaybackState::Paused)
	{
		if (!Player->Start(LastError))
		{
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
			return;
		}
		PlaybackState = EOpenVolumetricPlaybackState::Playing;
		if (AudioComponent != nullptr)
		{
			AudioComponent->SetPaused(false);
		}
	}
}

void UOpenVolumetricComponent::Pause()
{
	if (PlaybackState == EOpenVolumetricPlaybackState::Playing)
	{
		PlaybackState = EOpenVolumetricPlaybackState::Paused;
		if (AudioComponent != nullptr)
		{
			AudioComponent->SetPaused(true);
		}
	}
}

bool UOpenVolumetricComponent::Seek(double TimeSeconds)
{
	if (PlaybackState == EOpenVolumetricPlaybackState::Closed ||
		PlaybackState == EOpenVolumetricPlaybackState::Opening ||
		PlaybackState == EOpenVolumetricPlaybackState::Error)
	{
		return false;
	}

	CurrentTimeSeconds = FMath::Clamp(TimeSeconds, 0.0, DurationSeconds);
	if (!Player->Seek(CurrentTimeSeconds, LastError))
	{
		PlaybackState = EOpenVolumetricPlaybackState::Error;
		return false;
	}
	ResetAudio();
	return true;
}

void UOpenVolumetricComponent::Close()
{
	CloseAdaptiveMetrics();
	if (AudioComponent != nullptr)
	{
		AudioComponent->Stop();
	}
	if (ProceduralSoundWave != nullptr)
	{
		ProceduralSoundWave->ResetAudio();
	}
	if (Player)
	{
		Player->Stop();
		Player->Close();
	}
	PlaybackState = EOpenVolumetricPlaybackState::Closed;
	DurationSeconds = 0.0;
	CurrentTimeSeconds = 0.0;
	LastError.Reset();
	bRemoteSource = false;
	ResourceSizeBytes = -1;
	CachedBytes = 0;
	DownloadedBytes = 0;
	HttpRequestCount = 0;
	NetworkRecoveryCount = 0;
	bFragmentedInput = false;
	ActiveFragment = -1;
	FragmentCount = 0;
	CachedFragmentCount = 0;
	InputState = EOpenVolumetricInputState::Opening;
	bNetworkRebuffering = false;
	bResumeAfterNetworkRecovery = false;
	NetworkRecoveryTarget = 0.0;
	LastPresentationTime = -1.0;
	AdaptiveRepresentations.Reset();
	AdaptiveSegmentDuration = 0.0;
	AdaptiveSmoothedThroughputBps = 0.0;
	AdaptiveMetricsStarted = 0.0;
	AdaptiveNextMetricTime = 0.0;
	AdaptiveSwitchStarted = -1.0;
	AdaptiveLastSwitchLatency = -1.0;
	AdaptiveSwitchFailureCount = 0;
	AdaptiveRebufferStarted = -1.0;
	AdaptiveTotalRebufferTime = 0.0;
	AdaptiveRebufferCount = 0;
	AdaptivePreviousSwitchState = 0;
	AdaptivePreviousInputState = EOpenVolumetricInputState::Opening;
	PendingRepresentationId.Reset();
	AdaptiveSwitchCount = 0;
}

void UOpenVolumetricComponent::RecordAdaptiveMetrics()
{
	if (!bRecordAdaptiveMetrics || !bUseAdaptiveManifest || Player == nullptr)
		return;
	const double Now = FPlatformTime::Seconds();
	if (AdaptiveMetricsArchive == nullptr)
	{
		const FString SafeName = FPaths::GetCleanFilename(
			AdaptiveMetricsFileName.IsEmpty()
				? TEXT("openvolumetric-adaptive-metrics.csv")
				: AdaptiveMetricsFileName);
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), SafeName);
		AdaptiveMetricsArchive = IFileManager::Get().CreateFileWriter(*Path);
		if (AdaptiveMetricsArchive == nullptr)
		{
			UE_LOG(
				LogOpenVolumetricComponent,
				Error,
				TEXT("Could not create adaptive metrics file: %s"),
				*Path);
			bRecordAdaptiveMetrics = false;
			return;
		}
		WriteUtf8Line(
			*AdaptiveMetricsArchive,
			TEXT("wall_seconds,media_seconds,playback_state,input_state,")
			TEXT("active_representation,pending_representation,switch_state,")
			TEXT("throughput_mbps,downloaded_bytes,cached_bytes,http_requests,")
			TEXT("network_recoveries,active_fragment,cached_fragments,")
			TEXT("rebuffer_count,rebuffer_seconds,switch_count,")
			TEXT("switch_failures,last_switch_latency_seconds,presented_seconds,")
			TEXT("av_error_seconds,frame_ms,engine_memory_bytes,error"));
		AdaptiveMetricsStarted = Now;
		AdaptiveNextMetricTime = Now;
		UE_LOG(
			LogOpenVolumetricComponent,
			Log,
			TEXT("Recording adaptive metrics: %s"),
			*Path);
	}

	const openvolumetric::AdaptiveSwitchInfo SwitchInfo =
		Player->GetAdaptiveSwitchInfo();
	const int32 SwitchState = static_cast<int32>(SwitchInfo.state);
	if (InputState == EOpenVolumetricInputState::Rebuffering &&
		AdaptivePreviousInputState != EOpenVolumetricInputState::Rebuffering)
	{
		++AdaptiveRebufferCount;
		AdaptiveRebufferStarted = Now;
	}
	else if (InputState != EOpenVolumetricInputState::Rebuffering &&
		AdaptivePreviousInputState == EOpenVolumetricInputState::Rebuffering &&
		AdaptiveRebufferStarted >= 0.0)
	{
		AdaptiveTotalRebufferTime += Now - AdaptiveRebufferStarted;
		AdaptiveRebufferStarted = -1.0;
	}
	AdaptivePreviousInputState = InputState;

	if (SwitchInfo.state == openvolumetric::AdaptiveSwitchState::Preparing &&
		AdaptivePreviousSwitchState != SwitchState)
	{
		AdaptiveSwitchStarted = Now;
	}
	if (SwitchInfo.state == openvolumetric::AdaptiveSwitchState::Failed &&
		AdaptivePreviousSwitchState != SwitchState)
	{
		++AdaptiveSwitchFailureCount;
		if (AdaptiveSwitchStarted >= 0.0)
		{
			AdaptiveLastSwitchLatency = Now - AdaptiveSwitchStarted;
			AdaptiveSwitchStarted = -1.0;
		}
	}
	if (SwitchInfo.state == openvolumetric::AdaptiveSwitchState::Stable &&
		SwitchInfo.switch_count > 0 && AdaptivePreviousSwitchState != SwitchState &&
		AdaptiveSwitchStarted >= 0.0)
	{
		AdaptiveLastSwitchLatency = Now - AdaptiveSwitchStarted;
		AdaptiveSwitchStarted = -1.0;
	}
	AdaptivePreviousSwitchState = SwitchState;
	if (Now < AdaptiveNextMetricTime)
		return;
	AdaptiveNextMetricTime = Now + FMath::Max(0.05, AdaptiveMetricsIntervalSeconds);

	double RebufferSeconds = AdaptiveTotalRebufferTime;
	if (AdaptiveRebufferStarted >= 0.0)
		RebufferSeconds += Now - AdaptiveRebufferStarted;
	const UEnum* PlaybackEnum = StaticEnum<EOpenVolumetricPlaybackState>();
	const UEnum* InputEnum = StaticEnum<EOpenVolumetricInputState>();
	const FString SwitchStateText = SwitchInfo.state ==
		openvolumetric::AdaptiveSwitchState::Stable ? TEXT("Stable") :
		SwitchInfo.state == openvolumetric::AdaptiveSwitchState::Preparing ? TEXT("Preparing") :
		SwitchInfo.state == openvolumetric::AdaptiveSwitchState::Ready ? TEXT("Ready") :
		TEXT("Failed");
	const FString Line = FString::Printf(
		TEXT("%.6f,%.6f,%s,%s,%s,%s,%s,%.6f,%lld,%lld,%lld,%lld,%lld,%lld,%llu,%.6f,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%llu,%s"),
		Now - AdaptiveMetricsStarted,
		CurrentTimeSeconds,
		*Csv(PlaybackEnum->GetNameStringByValue(static_cast<int64>(PlaybackState))),
		*Csv(InputEnum->GetNameStringByValue(static_cast<int64>(InputState))),
		*Csv(UTF8_TO_TCHAR(SwitchInfo.active_representation.c_str())),
		*Csv(UTF8_TO_TCHAR(SwitchInfo.pending_representation.c_str())),
		*Csv(SwitchStateText),
		AdaptiveSmoothedThroughputBps / 1000000.0,
		DownloadedBytes,
		CachedBytes,
		HttpRequestCount,
		NetworkRecoveryCount,
		ActiveFragment,
		CachedFragmentCount,
		AdaptiveRebufferCount,
		RebufferSeconds,
		SwitchInfo.switch_count,
		AdaptiveSwitchFailureCount,
		AdaptiveLastSwitchLatency,
		LastPresentationTime,
		CurrentTimeSeconds - LastPresentationTime,
		AdaptiveMetricFrameMilliseconds,
		static_cast<uint64>(FPlatformMemory::GetStats().UsedPhysical),
		*Csv(LastError));
	WriteUtf8Line(*AdaptiveMetricsArchive, Line);
	AdaptiveMetricsArchive->Flush();
}

void UOpenVolumetricComponent::CloseAdaptiveMetrics()
{
	if (AdaptiveMetricsArchive == nullptr)
		return;
	AdaptiveMetricsArchive->Flush();
	AdaptiveMetricsArchive->Close();
	delete AdaptiveMetricsArchive;
	AdaptiveMetricsArchive = nullptr;
}

void UOpenVolumetricComponent::UpdateBufferDiagnostics()
{
	if (Player == nullptr)
	{
		return;
	}
	const openvolumetric::OpenVolumetricBufferInfo Info =
		Player->GetBufferInfo();
	// Core and reflected enums intentionally share stable numeric states.
	InputState = static_cast<EOpenVolumetricInputState>(Info.state);
	bRemoteSource = Info.remote;
	ResourceSizeBytes = static_cast<int64>(Info.resource_size_bytes);
	CachedBytes = static_cast<int64>(Info.cached_bytes);
	DownloadedBytes = static_cast<int64>(Info.downloaded_bytes);
	TransferThroughputBitsPerSecond = static_cast<int64>(
		Info.transfer_throughput_bits_per_second);
	HttpRequestCount = static_cast<int64>(Info.request_count);
	NetworkRecoveryCount = static_cast<int64>(Info.recovery_count);
	bFragmentedInput = Info.fragmented;
	ActiveFragment = static_cast<int64>(Info.active_fragment);
	FragmentCount = static_cast<int64>(Info.fragment_count);
	CachedFragmentCount = static_cast<int64>(Info.cached_fragment_count);
}

void UOpenVolumetricComponent::UpdateAdaptivePolicy()
{
	if (!bEnableLiveAdaptiveSwitching || !bUseAdaptiveManifest ||
		AdaptiveQuality != EOpenVolumetricAdaptiveQuality::Auto ||
		!bRemoteSource || AdaptiveRepresentations.Num() < 2 ||
		AdaptiveSegmentDuration <= 0.0 || Player == nullptr)
	{
		return;
	}

	Player->UpdateAdaptivePolicy(
		FPlatformTime::Seconds(),
		CurrentTimeSeconds,
		DurationSeconds,
		AdaptiveSegmentDuration);
	AdaptiveSmoothedThroughputBps = Player->GetAdaptivePolicyThroughput();
	const openvolumetric::AdaptiveSwitchInfo SwitchInfo =
		Player->GetAdaptiveSwitchInfo();
	SelectedRepresentationId = UTF8_TO_TCHAR(
		SwitchInfo.active_representation.c_str());
	PendingRepresentationId = UTF8_TO_TCHAR(
		SwitchInfo.pending_representation.c_str());
	AdaptiveSwitchCount = static_cast<int64>(SwitchInfo.switch_count);
}

bool UOpenVolumetricComponent::RequestAdaptiveHigh(bool bHigh)
{
	if (!bUseAdaptiveManifest || !bRemoteSource ||
		AdaptiveRepresentations.Num() < 2)
	{
		return false;
	}
	const int32 TargetIndex = bHigh
		? AdaptiveRepresentations.Num() - 1
		: 0;
	const openvolumetric::AdaptivePolicyDecision Decision =
		Player->RequestAdaptivePolicy(
		TargetIndex,
		FPlatformTime::Seconds(),
		CurrentTimeSeconds,
		DurationSeconds,
		AdaptiveSegmentDuration);
	return Decision.action != openvolumetric::AdaptivePolicyAction::RetryLater;
}

bool UOpenVolumetricComponent::HandleNetworkRecovery()
{
	if (!bRemoteSource)
	{
		return false;
	}
	if (InputState == EOpenVolumetricInputState::Rebuffering)
	{
		if (!bNetworkRebuffering)
		{
			bNetworkRebuffering = true;
			bResumeAfterNetworkRecovery =
				PlaybackState == EOpenVolumetricPlaybackState::Playing;
			NetworkRecoveryTarget = LastPresentationTime >= 0.0
				? LastPresentationTime
				: CurrentTimeSeconds;
			CurrentTimeSeconds = NetworkRecoveryTarget;
			PlaybackState = EOpenVolumetricPlaybackState::Paused;
			ResetAudio();
			UE_LOG(
				LogOpenVolumetricComponent,
				Warning,
				TEXT("HTTP input interrupted; rebuffering."));
		}
		return true;
	}
	if (!bNetworkRebuffering)
	{
		if (InputState == EOpenVolumetricInputState::Error)
		{
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			LastError = TEXT("HTTP input failed after recovery retries.");
			ResetAudio();
			return true;
		}
		return false;
	}
	if (InputState == EOpenVolumetricInputState::Error ||
		InputState == EOpenVolumetricInputState::Cancelled)
	{
		bNetworkRebuffering = false;
		PlaybackState = EOpenVolumetricPlaybackState::Error;
		LastError = TEXT("HTTP input failed after recovery retries.");
		return true;
	}
	if (InputState != EOpenVolumetricInputState::Ready)
	{
		return true;
	}

	bNetworkRebuffering = false;
	if (!Player->Seek(NetworkRecoveryTarget, LastError))
	{
		PlaybackState = EOpenVolumetricPlaybackState::Error;
		return true;
	}
	CurrentTimeSeconds = NetworkRecoveryTarget;
	ResetAudio();
	PlaybackState = bResumeAfterNetworkRecovery
		? EOpenVolumetricPlaybackState::Playing
		: EOpenVolumetricPlaybackState::Paused;
	UE_LOG(
		LogOpenVolumetricComponent,
		Log,
		TEXT("HTTP input recovered and playback resynchronized."));
	return true;
}

void UOpenVolumetricComponent::InitializeAudio()
{
	const openvolumetric::OpenVolumetricMediaInfo& Info = Player->GetMediaInfo();
	if (!Info.has_audio ||
		Info.audio_sample_rate <= 0 ||
		Info.audio_channels <= 0 ||
		GetOwner() == nullptr)
	{
		return;
	}

	if (AudioComponent == nullptr)
	{
		AudioComponent = NewObject<UAudioComponent>(
			GetOwner(), TEXT("OpenVolumetricAudio"));
		GetOwner()->AddInstanceComponent(AudioComponent);
		AudioComponent->bAutoActivate = false;
		AudioComponent->bAllowSpatialization =
			bEnableGeometryCentroidSpatialAudio;
		AudioComponent->bOverrideAttenuation = true;
		AudioComponent->AttenuationOverrides.bSpatialize = true;
		// Preserve the existing playback level. Projects can add distance
		// attenuation independently if their scene scale requires it.
		AudioComponent->AttenuationOverrides.bAttenuate = false;
		AudioComponent->AttenuationOverrides.StereoSpread = 0.0f;
		if (USceneComponent* Root = GetOwner()->GetRootComponent())
		{
			AudioComponent->SetupAttachment(Root);
		}
		AudioComponent->RegisterComponent();
	}

	ProceduralSoundWave = NewObject<USoundWaveProcedural>(this);
	ProceduralSoundWave->NumChannels = Info.audio_channels;
	ProceduralSoundWave->SetSampleRate(Info.audio_sample_rate);
	ProceduralSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
	ProceduralSoundWave->bLooping = false;
	ProceduralSoundWave->SoundGroup = SOUNDGROUP_Default;
	AudioComponent->SetSound(ProceduralSoundWave);
}

void UOpenVolumetricComponent::PumpAudio()
{
	if (Player == nullptr ||
		ProceduralSoundWave == nullptr ||
		AudioComponent == nullptr)
	{
		return;
	}

	const openvolumetric::OpenVolumetricMediaInfo& Info = Player->GetMediaInfo();
	const int32 BytesPerSecond =
		Info.audio_sample_rate * Info.audio_channels * sizeof(int16);
	// Maintain roughly 250 ms of queued audio. This is long enough to absorb
	// game-thread jitter without adding excessive A/V latency.
	const int32 TargetBytes = BytesPerSecond / 4;
	const int32 MissingBytes =
		TargetBytes - ProceduralSoundWave->GetAvailableAudioByteCount();
	if (MissingBytes <= 0)
	{
		return;
	}

	const int32 RequestedSamples = MissingBytes / sizeof(int16);
	const int32 SamplesRead =
		Player->ReadAudio(RequestedSamples, AudioSamples);
	if (SamplesRead > 0)
	{
		ProceduralSoundWave->QueueAudio(
			reinterpret_cast<const uint8*>(AudioSamples.GetData()),
			SamplesRead * sizeof(int16));
		if (!AudioComponent->IsPlaying())
		{
			AudioComponent->Play();
		}
	}
}

void UOpenVolumetricComponent::ResetAudio()
{
	bHasAudioCentroid = false;
	if (AudioComponent != nullptr)
	{
		AudioComponent->Stop();
	}
	if (ProceduralSoundWave != nullptr)
	{
		ProceduralSoundWave->ResetAudio();
	}
}

void UOpenVolumetricComponent::UpdateDeveloperControls(float DeltaTime)
{
	if (!bEnableDeveloperControls)
	{
		if (GEngine != nullptr && DeveloperMessageKey != 0)
		{
			GEngine->RemoveOnScreenDebugMessage(DeveloperMessageKey);
		}
		return;
	}

	APlayerController* PlayerController = GetWorld() != nullptr
		? GetWorld()->GetFirstPlayerController()
		: nullptr;
	if (PlayerController != nullptr)
	{
		if (PlayerController->WasInputKeyJustPressed(EKeys::K))
		{
			PlaybackState == EOpenVolumetricPlaybackState::Playing ? Pause() : Play();
		}
		if (PlayerController->WasInputKeyJustPressed(EKeys::J))
		{
			Seek(CurrentTimeSeconds - DeveloperSeekSeconds);
		}
		if (PlayerController->WasInputKeyJustPressed(EKeys::L))
		{
			Seek(CurrentTimeSeconds + DeveloperSeekSeconds);
		}
		if (PlayerController->WasInputKeyJustPressed(EKeys::O))
		{
			bLoop = !bLoop;
		}
		if (PlayerController->WasInputKeyJustPressed(EKeys::P))
		{
			const bool bCurrentlyHigh = AdaptiveRepresentations.Num() > 1 &&
				SelectedRepresentationId == AdaptiveRepresentations.Last().Id;
			RequestAdaptiveHigh(!bCurrentlyHigh);
		}
		if (PlayerController->WasInputKeyJustPressed(EKeys::I))
		{
			bDeveloperOverlayVisible = !bDeveloperOverlayVisible;
			if (!bDeveloperOverlayVisible && GEngine != nullptr)
			{
				GEngine->RemoveOnScreenDebugMessage(DeveloperMessageKey);
			}
		}
	}

	SmoothedDeveloperDeltaTime = FMath::Lerp(
		SmoothedDeveloperDeltaTime <= 0.0f
			? DeltaTime
			: SmoothedDeveloperDeltaTime,
		DeltaTime,
		0.05f);
	DeveloperStatusUpdateCountdown -= DeltaTime;
	if (!bDeveloperOverlayVisible || GEngine == nullptr ||
		DeveloperStatusUpdateCountdown > 0.0f)
	{
		return;
	}
	DeveloperStatusUpdateCountdown = 0.25f;

	const float FramesPerSecond = SmoothedDeveloperDeltaTime > 0.0f
		? 1.0f / SmoothedDeveloperDeltaTime
		: 0.0f;
	const FString StateName = StaticEnum<EOpenVolumetricPlaybackState>()->
		GetNameStringByValue(static_cast<int64>(PlaybackState));
	const FString InputName = StaticEnum<EOpenVolumetricInputState>()->
		GetNameStringByValue(static_cast<int64>(InputState));
	const double Megabytes = 1024.0 * 1024.0;
	FString NetworkStatus;
	if (bRemoteSource)
	{
		NetworkStatus = FString::Printf(
			TEXT("\nHTTP %s  cache:%.1f MB  downloaded:%.1f MB\nrequests:%lld  recoveries:%lld"),
			*InputName,
			static_cast<double>(CachedBytes) / Megabytes,
			static_cast<double>(DownloadedBytes) / Megabytes,
			HttpRequestCount,
			NetworkRecoveryCount);
	}
	if (bFragmentedInput)
	{
		NetworkStatus += FString::Printf(
			TEXT("\nfragments active:%lld/%lld  cached:%lld"),
			ActiveFragment >= 0 ? ActiveFragment + 1 : 0,
			FragmentCount,
			CachedFragmentCount);
	}
	if (!SelectedRepresentationId.IsEmpty())
	{
		NetworkStatus += AdaptiveMeasuredThroughputMbps > 0.0
			? FString::Printf(
				TEXT("\nquality: %s (%.1f Mbps probe)"),
				*SelectedRepresentationId,
				AdaptiveMeasuredThroughputMbps)
			: FString::Printf(TEXT("\nquality: %s"), *SelectedRepresentationId);
		if (!PendingRepresentationId.IsEmpty())
		{
			NetworkStatus += FString::Printf(
				TEXT("\npending: %s"), *PendingRepresentationId);
		}
		if (AdaptiveSwitchCount > 0)
		{
			NetworkStatus += FString::Printf(
				TEXT("  switches:%lld"), AdaptiveSwitchCount);
		}
	}
	const FString ErrorStatus = LastError.IsEmpty()
		? FString()
		: FString::Printf(TEXT("\nERROR: %s"), *LastError);
	const double UsedMemoryMb =
		static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / Megabytes;
	const FString StatusText = FString::Printf(
		TEXT("OPENVOLUMETRIC\n%s  %.1f/%.1fs  Loop:%s\n%.1f fps  %.2f ms  Memory:%.0f MB%s%s\n\nK Play/Pause  O Loop  P Quality\nJ -%.0fs  L +%.0fs  I Hide"),
		*StateName,
		CurrentTimeSeconds,
		DurationSeconds,
		bLoop ? TEXT("On") : TEXT("Off"),
		FramesPerSecond,
		SmoothedDeveloperDeltaTime * 1000.0f,
		UsedMemoryMb,
		*NetworkStatus,
		*ErrorStatus,
		DeveloperSeekSeconds,
		DeveloperSeekSeconds);
	GEngine->AddOnScreenDebugMessage(
		DeveloperMessageKey,
		0.3f,
		FColor(89, 255, 166),
		StatusText,
		false,
		FVector2D(1.0f, 1.0f));
}
