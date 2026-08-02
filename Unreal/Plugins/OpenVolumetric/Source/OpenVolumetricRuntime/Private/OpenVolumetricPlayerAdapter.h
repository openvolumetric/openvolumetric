#pragma once

#include "AdaptivePlayerCoordinator.h"

namespace UE::Geometry
{
class FDynamicMesh3;
}

/// Unreal-side owner of the engine-neutral player façade.
///
/// Keeping this type private prevents OpenVolumetricCore and standard-library
/// types from becoming part of the reflected UObject interface. Calls originate
/// on the game thread except ReadAudio(), which is invoked by procedural-audio
/// generation and delegates to the coordinator's thread-safe PCM consumer.
class FOpenVolumetricPlayerAdapter final
{
public:
	/// Opens one representation and reports a user-facing failure in OutError.
	bool Open(const FString& Path, FString& OutError);
	/// Starts native decoder workers after Open().
	bool Start(FString& OutError);
	/// Stops native workers while retaining opened state.
	void Stop();
	/// Seeks every modality to the same media timestamp.
	bool Seek(double TimeSeconds, FString& OutError);
	/// Labels the initially opened resource with its manifest identifier.
	void SetActiveRepresentationId(const FString& RepresentationId);
	/// Begins asynchronous candidate preparation for an aligned boundary.
	bool RequestAdaptiveSwitch(
		const FString& Resource,
		const FString& RepresentationId,
		double BoundaryTime,
		const FString& Reason,
		FString& OutError);
	/// Returns a thread-safe representation-transition snapshot.
	openvolumetric::AdaptiveSwitchInfo GetAdaptiveSwitchInfo() const;
	/// Cancels candidate preparation without interrupting the active session.
	void CancelAdaptiveSwitch();
	/// Returns the most recent core/coordinator failure.
	FString GetError() const;
	/// Idempotently releases all native playback resources.
	void Close();

	/// Returns immutable metadata valid while the resource remains open.
	const openvolumetric::OpenVolumetricMediaInfo& GetMediaInfo() const;
	/// Returns active HTTP/cache diagnostics.
	openvolumetric::OpenVolumetricBufferInfo GetBufferInfo() const;
	/// Polls and translates one complete presentation into Unreal-owned values.
	openvolumetric::FrameMatchResult PollPresentation(
		double TimeSeconds,
		double GeometryScale,
		float LuminanceCorrection,
		float BlueProjectionCorrection,
		float RedProjectionCorrection,
		UE::Geometry::FDynamicMesh3& OutMesh,
		FVector& OutGeometryCentroid,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight,
		double& OutPresentationTime);

	/// Reads interleaved float PCM and converts it to Unreal's signed PCM16.
	int32 ReadAudio(int32 SampleCount, TArray<int16>& OutSamples);

private:
	openvolumetric::AdaptivePlayerCoordinator Player;
	TArray<float> AudioFloatBuffer;
};
