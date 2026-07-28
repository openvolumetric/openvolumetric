#pragma once

#include "Components/ActorComponent.h"
#include "OpenVolComponent.generated.h"

class UDynamicMeshComponent;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USoundWaveProcedural;
class UTexture2D;

/** High-level lifecycle exposed to C++ and Blueprints. */
UENUM(BlueprintType)
enum class EOpenVolPlaybackState : uint8
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
 * Blueprint-facing owner of one OpenVol playback instance.
 *
 * This initial component establishes the Unreal API and lifecycle. Decoder,
 * dynamic-mesh, texture, material, and audio resources will sit behind it.
 */
UCLASS(
	ClassGroup = (OpenVol),
	BlueprintType,
	Blueprintable,
	meta = (BlueprintSpawnableComponent))
class OPENVOLRUNTIME_API UOpenVolComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UOpenVolComponent();
	virtual ~UOpenVolComponent() override;

	/** OpenVol MP4 path. Relative paths are resolved beneath Content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVol|Input")
	FFilePath SourceFile;

	/** Start playback automatically after a file opens successfully. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVol|Playback")
	bool bPlayOnOpen = true;

	/** Seek to the beginning when the presentation reaches its duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVol|Playback")
	bool bLoop = true;

	/** Converts OpenVol metres to Unreal centimetres by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVol|Geometry")
	double GeometryScale = 100.0;

	/** Runtime mesh created and updated by this player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVol|Geometry")
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

	/** Unlit material with a Texture2D parameter named OpenVolTexture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OpenVol|Texture")
	TObjectPtr<UMaterialInterface> TextureMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVol|Texture")
	TObjectPtr<UTexture2D> PresentationTexture;

	/** Procedural output for the audio track embedded in the OpenVol MP4. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "OpenVol|Audio")
	TObjectPtr<UAudioComponent> AudioComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVol|Status")
	EOpenVolPlaybackState PlaybackState = EOpenVolPlaybackState::Closed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVol|Status")
	double DurationSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVol|Status")
	double CurrentTimeSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "OpenVol|Status")
	FString LastError;

	/** Opens SourceFile and prepares native and engine resources. */
	UFUNCTION(BlueprintCallable, Category = "OpenVol|Playback")
	bool Open();

	UFUNCTION(BlueprintCallable, Category = "OpenVol|Playback")
	void Play();

	UFUNCTION(BlueprintCallable, Category = "OpenVol|Playback")
	void Pause();

	UFUNCTION(BlueprintCallable, Category = "OpenVol|Playback")
	bool Seek(double TimeSeconds);

	/** Stops playback and releases native and Unreal resources. */
	UFUNCTION(BlueprintCallable, Category = "OpenVol|Playback")
	void Close();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(
		float DeltaTime,
		ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	class FOpenVolPlayerAdapter* Player = nullptr;
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
