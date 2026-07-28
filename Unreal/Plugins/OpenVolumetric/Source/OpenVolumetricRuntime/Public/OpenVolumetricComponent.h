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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVolumetric|Texture")
	TObjectPtr<UTexture2D> PresentationTexture;

	/** Procedural output for the audio track embedded in the OpenVolumetric MP4. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVolumetric|Audio")
	TObjectPtr<UAudioComponent> AudioComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	EOpenVolumetricPlaybackState PlaybackState = EOpenVolumetricPlaybackState::Closed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	double DurationSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	double CurrentTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVolumetric|Status")
	FString LastError;

	/** Opens SourceFile and prepares native and engine resources. */
	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	bool Open();

	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	void Play();

	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	void Pause();

	UFUNCTION(BlueprintCallable, Category = "OpenVolumetric|Playback")
	bool Seek(double TimeSeconds);

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
	void CreateDynamicMeshComponent();
	void UpdatePresentationTexture(
		const TArray<FColor>& Pixels,
		int32 Width,
		int32 Height);
	void InitializeAudio();
	void PumpAudio();
	void ResetAudio();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> ProceduralSoundWave;

	TArray<int16> AudioSamples;
};
