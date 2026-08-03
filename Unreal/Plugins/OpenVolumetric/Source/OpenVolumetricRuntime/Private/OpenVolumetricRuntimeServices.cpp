#include "OpenVolumetricRuntimeServices.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

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

void FOpenVolumetricPresentationUploader::UpdateTexture(
	UObject* Owner,
	UDynamicMeshComponent* MeshComponent,
	UMaterialInterface* Material,
	const TArray<FColor>& Pixels,
	int32 Width,
	int32 Height,
	UTexture2D*& Texture,
	UMaterialInstanceDynamic*& DynamicMaterial)
{
	if (Pixels.Num() != Width * Height || Width <= 0 || Height <= 0)
		return;
	if (Texture == nullptr || Texture->GetSizeX() != Width ||
		Texture->GetSizeY() != Height)
	{
		Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
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
	uint8* Upload = static_cast<uint8*>(FMemory::Malloc(ByteCount));
	FMemory::Memcpy(Upload, Pixels.GetData(), ByteCount);
	FUpdateTextureRegion2D* Region =
		new FUpdateTextureRegion2D(0, 0, 0, 0, Width, Height);
	Texture->UpdateTextureRegions(
		0, 1, Region, Width * sizeof(FColor), sizeof(FColor), Upload,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
}
