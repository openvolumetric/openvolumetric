#pragma once

#include "OpenVolPlayer.h"

namespace UE::Geometry
{
class FDynamicMesh3;
}

/**
 * Unreal-side owner of the engine-neutral player façade.
 *
 * Keeping this type private prevents OpenVolCore and standard-library types
 * from becoming part of the reflected UObject interface.
 */
class FOpenVolPlayerAdapter final
{
public:
	bool Open(const FString& Path, FString& OutError);
	bool Start(FString& OutError);
	void Stop();
	bool Seek(double TimeSeconds, FString& OutError);
	void Close();

	const openvol::OpenVolMediaInfo& GetMediaInfo() const;
	openvol::FrameMatchResult PollPresentation(
		double TimeSeconds,
		double GeometryScale,
		UE::Geometry::FDynamicMesh3& OutMesh,
		TArray<FColor>& OutPixels,
		int32& OutWidth,
		int32& OutHeight,
		double& OutPresentationTime);

	/** Reads interleaved float PCM and converts it to Unreal's signed PCM16. */
	int32 ReadAudio(int32 SampleCount, TArray<int16>& OutSamples);

private:
	openvol::OpenVolPlayer Player;
	TArray<float> AudioFloatBuffer;
};
