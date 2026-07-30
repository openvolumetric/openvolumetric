#include "OpenVolumetricComponent.h"

#include "OpenVolumetricPlayerAdapter.h"
#include "Components/AudioComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenVolumetricComponent, Log, All);

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
	delete Player;
	Player = nullptr;
}

void UOpenVolumetricComponent::BeginPlay()
{
	Super::BeginPlay();
	CreateDynamicMeshComponent();
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
	UpdateBufferDiagnostics();
	if (HandleNetworkRecovery())
	{
		return;
	}
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
			Pixels,
			TextureWidth,
			TextureHeight,
			PresentationTime);
	if (Result == openvolumetric::FrameMatchResult::Ready)
	{
		LastPresentationTime = PresentationTime;
		DynamicMeshComponent->SetMesh(MoveTemp(Mesh));
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
		LastError = TEXT("No OpenVolumetric MP4 has been selected.");
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
				TEXT("OpenVolumetric MP4 does not exist: %s"), *ResolvedPath);
			PlaybackState = EOpenVolumetricPlaybackState::Error;
			UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
			return false;
		}
	}

	PlaybackState = EOpenVolumetricPlaybackState::Opening;
	if (!Player->Open(ResolvedPath, LastError))
	{
		PlaybackState = EOpenVolumetricPlaybackState::Error;
		UE_LOG(LogOpenVolumetricComponent, Error, TEXT("%s"), *LastError);
		return false;
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
	InputState = EOpenVolumetricInputState::Opening;
	bNetworkRebuffering = false;
	bResumeAfterNetworkRecovery = false;
	NetworkRecoveryTarget = 0.0;
	LastPresentationTime = -1.0;
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
	HttpRequestCount = static_cast<int64>(Info.request_count);
	NetworkRecoveryCount = static_cast<int64>(Info.recovery_count);
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
		AudioComponent->bAllowSpatialization = false;
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
	if (AudioComponent != nullptr)
	{
		AudioComponent->Stop();
	}
	if (ProceduralSoundWave != nullptr)
	{
		ProceduralSoundWave->ResetAudio();
	}
}
