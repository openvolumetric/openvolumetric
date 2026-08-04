#include "OpenVolumetricRuntimeServices.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

#include <array>

DEFINE_LOG_CATEGORY_STATIC(LogOpenVolumetricUpload, Log, All);

namespace
{
constexpr int32 OpenVolumetricUploadSlotCount = 3;
}

struct FOpenVolumetricPresentationUploader::FUploadState final
{
	struct FSlot
	{
		TArray<uint8> Bytes;
		FUpdateTextureRegion2D Region{0, 0, 0, 0, 0, 0};
		TAtomic<bool> bInFlight{false};
	};

	std::array<FSlot, OpenVolumetricUploadSlotCount> Slots;
	int32 NextSlot = 0;
};

FOpenVolumetricPresentationUploader::FOpenVolumetricPresentationUploader()
	: UploadState(MakeShared<FUploadState, ESPMode::ThreadSafe>())
{
}

FOpenVolumetricPresentationUploader::~FOpenVolumetricPresentationUploader() = default;

EOpenVolumetricClockAction FOpenVolumetricPlaybackClock::Advance(
	double DeltaSeconds,
	double DurationSeconds,
	bool bLoop,
	double& CurrentTimeSeconds) const
{
	CurrentTimeSeconds += DeltaSeconds;
	if (DurationSeconds <= 0.0 || CurrentTimeSeconds < DurationSeconds)
		return EOpenVolumetricClockAction::Continue;
	return bLoop
		? EOpenVolumetricClockAction::Loop
		: EOpenVolumetricClockAction::End;
}

EOpenVolumetricRecoveryAction FOpenVolumetricNetworkRecovery::Update(
	bool bRemote,
	int32 InputState,
	bool bPlaying,
	double PresentedTime,
	double CurrentTime)
{
	if (!bRemote)
		return EOpenVolumetricRecoveryAction::None;
	// Values mirror the stable core/Unreal input-state enum.
	constexpr int32 Ready = 1;
	constexpr int32 Rebuffering = 2;
	constexpr int32 Error = 3;
	constexpr int32 Cancelled = 4;
	if (InputState == Rebuffering)
	{
		if (!bRebuffering)
		{
			bRebuffering = true;
			bResume = bPlaying;
			RecoveryTarget = PresentedTime >= 0.0 ? PresentedTime : CurrentTime;
			return EOpenVolumetricRecoveryAction::BeginRebuffer;
		}
		return EOpenVolumetricRecoveryAction::Wait;
	}
	if (!bRebuffering)
		return InputState == Error
			? EOpenVolumetricRecoveryAction::Fail
			: EOpenVolumetricRecoveryAction::None;
	if (InputState == Error || InputState == Cancelled)
	{
		bRebuffering = false;
		return EOpenVolumetricRecoveryAction::Fail;
	}
	if (InputState != Ready)
		return EOpenVolumetricRecoveryAction::Wait;
	bRebuffering = false;
	return EOpenVolumetricRecoveryAction::Resume;
}

void FOpenVolumetricNetworkRecovery::Reset()
{
	bRebuffering = false;
	bResume = false;
	RecoveryTarget = 0.0;
}

bool FOpenVolumetricPresentationUploader::UpdateTexture(
	UObject* Owner,
	UDynamicMeshComponent* MeshComponent,
	UMaterialInterface* Material,
	const TArray<FColor>& Pixels,
	int32 Width,
	int32 Height,
	UTexture2D*& Texture,
	UMaterialInstanceDynamic*& DynamicMaterial)
{
	if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0 ||
		!UploadState.IsValid())
	{
		UE_LOG(LogOpenVolumetricUpload, Error,
			TEXT("Texture upload rejected invalid dimensions or pixel storage."));
		return false;
	}
	if (Texture == nullptr || Texture->GetSizeX() != Width ||
		Texture->GetSizeY() != Height)
	{
		Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
		if (Texture == nullptr)
		{
			UE_LOG(LogOpenVolumetricUpload, Error,
				TEXT("CreateTransient failed for %dx%d presentation texture."),
				Width, Height);
			return false;
		}
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->UpdateResource();
		if (Material != nullptr && MeshComponent != nullptr)
		{
			DynamicMaterial = UMaterialInstanceDynamic::Create(Material, Owner);
			DynamicMaterial->SetTextureParameterValue(
				TEXT("OpenVolumetricTexture"), Texture);
			MeshComponent->SetMaterial(0, DynamicMaterial);
		}
	}
	const SIZE_T ByteCount = static_cast<SIZE_T>(Pixels.Num()) * sizeof(FColor);
	int32 SlotIndex = INDEX_NONE;
	for (int32 Attempt = 0; Attempt < OpenVolumetricUploadSlotCount; ++Attempt)
	{
		const int32 Candidate =
			(UploadState->NextSlot + Attempt) % OpenVolumetricUploadSlotCount;
		if (!UploadState->Slots[Candidate].bInFlight.Load())
		{
			UploadState->Slots[Candidate].bInFlight.Store(true);
			SlotIndex = Candidate;
			UploadState->NextSlot =
				(Candidate + 1) % OpenVolumetricUploadSlotCount;
			break;
		}
	}
	if (SlotIndex == INDEX_NONE)
	{
		++Metrics.DroppedFrames;
		UE_LOG(LogOpenVolumetricUpload, Warning,
			TEXT("Texture upload ring is full; presentation frame dropped."));
		return false;
	}

	FUploadState::FSlot& Slot = UploadState->Slots[SlotIndex];
	if (ByteCount > static_cast<SIZE_T>(Slot.Bytes.Max()))
		++Metrics.StorageGrowths;
	Slot.Bytes.SetNumUninitialized(ByteCount, EAllowShrinking::No);
	FMemory::Memcpy(Slot.Bytes.GetData(), Pixels.GetData(), ByteCount);
	Slot.Region = FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
	const TSharedPtr<FUploadState, ESPMode::ThreadSafe> State = UploadState;
	Texture->UpdateTextureRegions(
		0,
		1,
		&Slot.Region,
		Width * sizeof(FColor),
		sizeof(FColor),
		Slot.Bytes.GetData(),
		[State, SlotIndex](uint8*, const FUpdateTextureRegion2D*)
		{
			State->Slots[SlotIndex].bInFlight.Store(false);
		});
	++Metrics.SubmittedFrames;
	return true;
}

FOpenVolumetricUploadMetrics
FOpenVolumetricPresentationUploader::GetMetrics() const
{
	return Metrics;
}
