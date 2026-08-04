#include "OpenVolumetricAuthoringValidation.h"

#include "AuthoringWorkflow.h"
#include "HAL/FileManager.h"

#include <filesystem>

namespace
{
bool DiscoverSequence(
	const FString& Directory,
	const TSet<FString>& Extensions,
	const TCHAR* Label,
	TArray<FOpenVolumetricNumberedFile>& OutFiles,
	FString& OutError)
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *(Directory / TEXT("*")), true, false);
	for (const FString& Name : Names)
	{
		const FString Extension = FPaths::GetExtension(Name, true).ToLower();
		const FString Stem = FPaths::GetBaseFilename(Name);
		if (!Extensions.Contains(Extension) || !Stem.IsNumeric()) continue;
		FOpenVolumetricNumberedFile& File = OutFiles.AddDefaulted_GetRef();
		File.Frame = FCString::Atoi(*Stem);
		File.Stem = Stem;
		File.Extension = Extension;
		File.Path = Directory / Name;
	}
	OutFiles.Sort([](const auto& A, const auto& B) { return A.Frame < B.Frame; });
	if (OutFiles.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("No numbered %s were found in %s."), Label, *Directory);
		return false;
	}
	for (int32 Index = 1; Index < OutFiles.Num(); ++Index)
	{
		if (OutFiles[Index].Frame != OutFiles[Index - 1].Frame + 1)
		{
			OutError = FString::Printf(
				TEXT("%s have a gap between frames %d and %d."), Label,
				OutFiles[Index - 1].Frame, OutFiles[Index].Frame);
			return false;
		}
		if (OutFiles[Index].Stem.Len() != OutFiles[0].Stem.Len() ||
			OutFiles[Index].Extension != OutFiles[0].Extension)
		{
			OutError = FString::Printf(
				TEXT("All %s must use identical padding and extensions."), Label);
			return false;
		}
	}
	return true;
}
}

bool OpenVolumetricAuthoringValidation::Validate(
	const FString& ImageDirectory,
	const FString& GeometryDirectory,
	const FString& AudioFile,
	const FString& OutputFile,
	const FString& FFmpeg,
	double FrameRate,
	bool bOverwrite,
	bool bAdaptive,
	FOpenVolumetricEncodingInputs& OutInputs,
	FString& OutError)
{
	if (!IFileManager::Get().DirectoryExists(*ImageDirectory) ||
		!IFileManager::Get().DirectoryExists(*GeometryDirectory))
	{
		OutError = TEXT("Choose existing image and OBJ directories.");
		return false;
	}
	if (!IFileManager::Get().FileExists(*FFmpeg))
	{
		OutError = TEXT("FFmpeg was not found. Choose its executable.");
		return false;
	}
	if (FrameRate <= 0.0)
	{
		OutError = TEXT("Frame rate must be greater than zero.");
		return false;
	}
	if (!AudioFile.IsEmpty() && !IFileManager::Get().FileExists(*AudioFile))
	{
		OutError = TEXT("The selected audio file does not exist.");
		return false;
	}
	if (!bAdaptive && OutputFile.IsEmpty())
	{
		OutError = TEXT("Choose an output MP4.");
		return false;
	}
	if (!bAdaptive && IFileManager::Get().FileExists(*OutputFile) && !bOverwrite)
	{
		OutError = TEXT("The output exists. Enable overwrite or choose another path.");
		return false;
	}

	openvolumetric::authoring::SourceSequenceInfo SequenceInfo;
	std::string NativeError;
	if (!openvolumetric::authoring::validate_source_sequences(
		std::filesystem::path(TCHAR_TO_UTF8(*ImageDirectory)),
		std::filesystem::path(TCHAR_TO_UTF8(*GeometryDirectory)),
		SequenceInfo,
		NativeError))
	{
		OutError = UTF8_TO_TCHAR(NativeError.c_str());
		return false;
	}

	return DiscoverSequence(
		ImageDirectory,
		{TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"), TEXT(".tif"),
		 TEXT(".tiff"), TEXT(".exr")},
		TEXT("images"), OutInputs.Images, OutError) &&
		DiscoverSequence(
			GeometryDirectory, {TEXT(".obj")}, TEXT("OBJ meshes"),
			OutInputs.Geometry, OutError);
}

FString OpenVolumetricAuthoringValidation::FindFFmpeg()
{
	const TArray<FString> Candidates = {
		TEXT("/opt/homebrew/bin/ffmpeg"),
		TEXT("/usr/local/bin/ffmpeg"),
		TEXT("/usr/bin/ffmpeg")};
	for (const FString& Candidate : Candidates)
		if (IFileManager::Get().FileExists(*Candidate)) return Candidate;
	return {};
}
