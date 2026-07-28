#include "OpenVolComponent.h"

#include "OpenVolPlayerAdapter.h"
#include "Components/AudioComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenVolComponent, Log, All);

UOpenVolComponent::UOpenVolComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	Player = new FOpenVolPlayerAdapter();

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
		TEXT("/OpenVol/Materials/M_OpenVolTexture.M_OpenVolTexture"));
	if (Material.Succeeded())
	{
		TextureMaterial = Material.Object;
	}
}

UOpenVolComponent::~UOpenVolComponent()
{
	delete Player;
	Player = nullptr;
}

void UOpenVolComponent::BeginPlay()
{
	Super::BeginPlay();
	CreateDynamicMeshComponent();
	if (!SourceFile.FilePath.IsEmpty())
	{
		Open();
	}
}

void UOpenVolComponent::CreateDynamicMeshComponent()
{
	if (DynamicMeshComponent != nullptr || GetOwner() == nullptr)
	{
		return;
	}

	DynamicMeshComponent = NewObject<UDynamicMeshComponent>(
		GetOwner(), TEXT("OpenVolDynamicMesh"));
	GetOwner()->AddInstanceComponent(DynamicMeshComponent);
	if (USceneComponent* Root = GetOwner()->GetRootComponent())
	{
		DynamicMeshComponent->SetupAttachment(Root);
	}
	DynamicMeshComponent->SetCollisionEnabled(
		ECollisionEnabled::NoCollision);
	DynamicMeshComponent->SetCastShadow(false);
	DynamicMeshComponent->RegisterComponent();
}

void UOpenVolComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (PlaybackState != EOpenVolPlaybackState::Playing ||
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
				PlaybackState = EOpenVolPlaybackState::Error;
				return;
			}
			CurrentTimeSeconds = 0.0;
			ResetAudio();
		}
		else
		{
			PlaybackState = EOpenVolPlaybackState::Ended;
			return;
		}
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	TArray<FColor> Pixels;
	int32 TextureWidth = 0;
	int32 TextureHeight = 0;
	double PresentationTime = 0.0;
	const openvol::FrameMatchResult Result =
		Player->PollPresentation(
			CurrentTimeSeconds,
			GeometryScale,
			Mesh,
			Pixels,
			TextureWidth,
			TextureHeight,
			PresentationTime);
	if (Result == openvol::FrameMatchResult::Ready)
	{
		DynamicMeshComponent->SetMesh(MoveTemp(Mesh));
		UpdatePresentationTexture(
			Pixels, TextureWidth, TextureHeight);
	}
}

void UOpenVolComponent::UpdatePresentationTexture(
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
				TEXT("OpenVolTexture"), PresentationTexture);
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

void UOpenVolComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Close();
	Super::EndPlay(EndPlayReason);
}

bool UOpenVolComponent::Open()
{
	Close();

	if (SourceFile.FilePath.IsEmpty())
	{
		LastError = TEXT("No OpenVol MP4 has been selected.");
		PlaybackState = EOpenVolPlaybackState::Error;
		UE_LOG(LogOpenVolComponent, Error, TEXT("%s"), *LastError);
		return false;
	}

	FString ResolvedPath = SourceFile.FilePath;
	FPaths::NormalizeFilename(ResolvedPath);
	if (FPaths::IsRelative(ResolvedPath))
	{
		// FFilePath may serialize a picker result relative to the editor
		// process, the project, or Content. Test the conventional locations
		// rather than blindly prefixing Content and duplicating the path.
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
			TEXT("OpenVol MP4 does not exist: %s"), *ResolvedPath);
		PlaybackState = EOpenVolPlaybackState::Error;
		UE_LOG(LogOpenVolComponent, Error, TEXT("%s"), *LastError);
		return false;
	}

	PlaybackState = EOpenVolPlaybackState::Opening;
	if (!Player->Open(ResolvedPath, LastError))
	{
		PlaybackState = EOpenVolPlaybackState::Error;
		UE_LOG(LogOpenVolComponent, Error, TEXT("%s"), *LastError);
		return false;
	}

	const openvol::OpenVolMediaInfo& Info = Player->GetMediaInfo();
	DurationSeconds = Info.duration;
	CurrentTimeSeconds = 0.0;
	PlaybackState = EOpenVolPlaybackState::Ready;
	LastError.Reset();
	InitializeAudio();

	UE_LOG(
		LogOpenVolComponent,
		Log,
		TEXT("Opened OpenVol MP4: %dx%d, %.3f fps, %.3f seconds, audio=%s"),
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

void UOpenVolComponent::Play()
{
	if (PlaybackState == EOpenVolPlaybackState::Ready ||
		PlaybackState == EOpenVolPlaybackState::Paused)
	{
		if (!Player->Start(LastError))
		{
			PlaybackState = EOpenVolPlaybackState::Error;
			UE_LOG(LogOpenVolComponent, Error, TEXT("%s"), *LastError);
			return;
		}
		PlaybackState = EOpenVolPlaybackState::Playing;
		if (AudioComponent != nullptr)
		{
			AudioComponent->SetPaused(false);
		}
	}
}

void UOpenVolComponent::Pause()
{
	if (PlaybackState == EOpenVolPlaybackState::Playing)
	{
		PlaybackState = EOpenVolPlaybackState::Paused;
		if (AudioComponent != nullptr)
		{
			AudioComponent->SetPaused(true);
		}
	}
}

bool UOpenVolComponent::Seek(double TimeSeconds)
{
	if (PlaybackState == EOpenVolPlaybackState::Closed ||
		PlaybackState == EOpenVolPlaybackState::Opening ||
		PlaybackState == EOpenVolPlaybackState::Error)
	{
		return false;
	}

	CurrentTimeSeconds = FMath::Clamp(TimeSeconds, 0.0, DurationSeconds);
	if (!Player->Seek(CurrentTimeSeconds, LastError))
	{
		PlaybackState = EOpenVolPlaybackState::Error;
		return false;
	}
	ResetAudio();
	return true;
}

void UOpenVolComponent::Close()
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
	PlaybackState = EOpenVolPlaybackState::Closed;
	DurationSeconds = 0.0;
	CurrentTimeSeconds = 0.0;
	LastError.Reset();
}

void UOpenVolComponent::InitializeAudio()
{
	const openvol::OpenVolMediaInfo& Info = Player->GetMediaInfo();
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
			GetOwner(), TEXT("OpenVolAudio"));
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

void UOpenVolComponent::PumpAudio()
{
	if (Player == nullptr ||
		ProceduralSoundWave == nullptr ||
		AudioComponent == nullptr)
	{
		return;
	}

	const openvol::OpenVolMediaInfo& Info = Player->GetMediaInfo();
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

void UOpenVolComponent::ResetAudio()
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
