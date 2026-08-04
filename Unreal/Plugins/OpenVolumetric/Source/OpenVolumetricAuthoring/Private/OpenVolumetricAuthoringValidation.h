#pragma once

#include "CoreMinimal.h"

/** One numbered source file, retaining the original padding and extension. */
struct FOpenVolumetricNumberedFile
{
	int32 Frame = 0;
	FString Stem;
	FString Extension;
	FString Path;
};

/** Immutable, frame-aligned source sequences passed to an authoring job. */
struct FOpenVolumetricEncodingInputs
{
	TArray<FOpenVolumetricNumberedFile> Images;
	TArray<FOpenVolumetricNumberedFile> Geometry;
};

namespace OpenVolumetricAuthoringValidation
{
bool Validate(
	const FString& ImageDirectory,
	const FString& GeometryDirectory,
	const FString& AudioFile,
	const FString& OutputFile,
	const FString& FFmpeg,
	double FrameRate,
	bool bOverwrite,
	bool bAdaptive,
	FOpenVolumetricEncodingInputs& OutInputs,
	FString& OutError);

FString FindFFmpeg();
}
