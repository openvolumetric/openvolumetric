#include "OpenVolumetricEncoderWindow.h"

#include "AdaptivePackage.h"
#include "Async/Async.h"
#include "AuthoringWorkflow.h"
#include "DesktopPlatformModule.h"
#include "DracoMeshEncoder.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "VolumetricVideoPacker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

#include <filesystem>

namespace
{
const TCHAR* SettingsSection = TEXT("OpenVolumetric.Authoring");

enum class EOpenVolumetricPreset : uint8
{
	DesktopLocal,
	DesktopStreaming,
	QuestLocal,
	QuestStreaming
};

struct FEncodingSettings
{
	FString Codec;
	int32 Crf;
	int32 Keyframes;
	int32 References;
	bool bDisableSao;
	int32 PositionBits;
	int32 NormalBits;
	int32 UVBits;
	int32 EncodeSpeed;
	int32 DecodeSpeed;
	int32 MaximumVideoBitrateKbps;
	int32 VideoBufferSizeKbps;
	int32 GeometryKeyframeInterval;
};

openvolumetric::authoring::PlatformPreset GetNativePreset(
	EOpenVolumetricPreset Preset)
{
	using openvolumetric::authoring::PlatformPreset;
	switch (Preset)
	{
	case EOpenVolumetricPreset::DesktopLocal:
		return PlatformPreset::DesktopLocal;
	case EOpenVolumetricPreset::DesktopStreaming:
		return PlatformPreset::DesktopStreaming;
	case EOpenVolumetricPreset::QuestStreaming:
		return PlatformPreset::QuestStreaming;
	case EOpenVolumetricPreset::QuestLocal:
	default:
		return PlatformPreset::QuestLocal;
	}
}

FEncodingSettings ToUnrealSettings(
	const openvolumetric::authoring::EncodingSettings& Settings)
{
	return {
		Settings.codec == openvolumetric::authoring::VideoCodec::HEVC
			? TEXT("libx265")
			: TEXT("libx264"),
		Settings.crf,
		Settings.video_keyframe_interval,
		Settings.reference_frames,
		Settings.disable_sao,
		Settings.position_quantization,
		Settings.normal_quantization,
		Settings.texture_quantization,
		Settings.draco_encode_speed,
		Settings.draco_decode_speed,
		Settings.maximum_video_bitrate_kbps,
		Settings.video_buffer_size_kbps,
		Settings.geometry_keyframe_interval};
}

struct FNumberedFile
{
	int32 Frame = 0;
	FString Stem;
	FString Extension;
	FString Path;
};

struct FEncodingInputs
{
	TArray<FNumberedFile> Images;
	TArray<FNumberedFile> Geometry;
};

struct FAuthoringState final
{
	TAtomic<bool> bRunning{false};
	TAtomic<bool> bCancel{false};
	TAtomic<float> Progress{0.0f};
	FCriticalSection TextMutex;
	FString Status = TEXT("Ready");
	FString Log;

	void SetStatus(const FString& Value)
	{
		FScopeLock Lock(&TextMutex);
		Status = Value;
	}

	void Append(const FString& Value)
	{
		FScopeLock Lock(&TextMutex);
		Log += Value + LINE_TERMINATOR;
	}

	FString GetStatus() const
	{
		FScopeLock Lock(
			const_cast<FCriticalSection*>(&TextMutex));
		return Status;
	}

	FString GetLog() const
	{
		FScopeLock Lock(
			const_cast<FCriticalSection*>(&TextMutex));
		return Log;
	}
};

FEncodingSettings GetPreset(EOpenVolumetricPreset Preset)
{
	return ToUnrealSettings(openvolumetric::authoring::preset_settings(
		GetNativePreset(Preset)));
}

FString Quote(const FString& Value)
{
	return FString::Printf(
		TEXT("\"%s\""),
		*Value.Replace(TEXT("\""), TEXT("\\\"")));
}

bool DiscoverSequence(
	const FString& Directory,
	const TSet<FString>& Extensions,
	const TCHAR* Label,
	TArray<FNumberedFile>& OutFiles,
	FString& OutError)
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(
		Names, *(Directory / TEXT("*")), true, false);
	for (const FString& Name : Names)
	{
		const FString Extension =
			FPaths::GetExtension(Name, true).ToLower();
		const FString Stem = FPaths::GetBaseFilename(Name);
		if (!Extensions.Contains(Extension) || !Stem.IsNumeric())
		{
			continue;
		}
		FNumberedFile& File = OutFiles.AddDefaulted_GetRef();
		File.Frame = FCString::Atoi(*Stem);
		File.Stem = Stem;
		File.Extension = Extension;
		File.Path = Directory / Name;
	}
	OutFiles.Sort(
		[](const FNumberedFile& A, const FNumberedFile& B)
		{
			return A.Frame < B.Frame;
		});
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
				TEXT("%s have a gap between frames %d and %d."),
				Label,
				OutFiles[Index - 1].Frame,
				OutFiles[Index].Frame);
			return false;
		}
		if (OutFiles[Index].Stem.Len() != OutFiles[0].Stem.Len() ||
			OutFiles[Index].Extension != OutFiles[0].Extension)
		{
			OutError = FString::Printf(
				TEXT("All %s must use identical padding and extensions."),
				Label);
			return false;
		}
	}
	return true;
}

bool Validate(
	const FString& ImageDirectory,
	const FString& GeometryDirectory,
	const FString& AudioFile,
	const FString& OutputFile,
	const FString& FFmpeg,
	double FrameRate,
	bool bOverwrite,
	bool bAdaptive,
	FEncodingInputs& OutInputs,
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
	if (!AudioFile.IsEmpty() &&
		!IFileManager::Get().FileExists(*AudioFile))
	{
		OutError = TEXT("The selected audio file does not exist.");
		return false;
	}
	if (!bAdaptive && OutputFile.IsEmpty())
	{
		OutError = TEXT("Choose an output MP4.");
		return false;
	}
	if (!bAdaptive &&
		IFileManager::Get().FileExists(*OutputFile) && !bOverwrite)
	{
		OutError =
			TEXT("The output exists. Enable overwrite or choose another path.");
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

	if (!DiscoverSequence(
			ImageDirectory,
			{TEXT(".png"), TEXT(".jpg"), TEXT(".jpeg"),
			 TEXT(".tif"), TEXT(".tiff"), TEXT(".exr")},
			TEXT("images"),
			OutInputs.Images,
			OutError) ||
		!DiscoverSequence(
			GeometryDirectory,
			{TEXT(".obj")},
			TEXT("OBJ meshes"),
			OutInputs.Geometry,
			OutError))
	{
		return false;
	}
	return true;
}

FString FindFFmpeg()
{
	const TArray<FString> Candidates = {
		TEXT("/opt/homebrew/bin/ffmpeg"),
		TEXT("/usr/local/bin/ffmpeg"),
		TEXT("/usr/bin/ffmpeg")};
	for (const FString& Candidate : Candidates)
	{
		if (IFileManager::Get().FileExists(*Candidate))
		{
			return Candidate;
		}
	}
	return {};
}
}

class SOpenVolumetricEncoderWindow::FImpl
{
public:
	TSharedRef<FAuthoringState, ESPMode::ThreadSafe> State =
		MakeShared<FAuthoringState, ESPMode::ThreadSafe>();
	EOpenVolumetricPreset Preset = EOpenVolumetricPreset::QuestLocal;
	TArray<TSharedPtr<FString>> PresetNames;
	TSharedPtr<FString> SelectedPreset;
	TSharedPtr<SEditableTextBox> Images;
	TSharedPtr<SEditableTextBox> Geometry;
	TSharedPtr<SEditableTextBox> Audio;
	TSharedPtr<SEditableTextBox> Output;
	TSharedPtr<SEditableTextBox> AdaptiveOutputFolder;
	TSharedPtr<SEditableTextBox> PresentationName;
	TSharedPtr<SEditableTextBox> FFmpeg;
	TSharedPtr<SEditableTextBox> FrameRate;
	TSharedPtr<SEditableTextBox> MaximumGeometryFrames;
	TSharedPtr<SEditableTextBox> FragmentDuration;
	bool bOverwrite = false;
	bool bGeometryCompression = true;
	bool bLimitGeometryKeyframeInterval = false;
	bool bFragmentedMp4 = false;
	bool bAdaptivePackage = false;

	FImpl()
	{
		PresetNames = {
			MakeShared<FString>(TEXT("Desktop Local")),
			MakeShared<FString>(TEXT("Desktop Streaming")),
			MakeShared<FString>(TEXT("Quest Local")),
			MakeShared<FString>(TEXT("Quest Streaming"))};
		SelectedPreset = PresetNames[2];
	}

	void Load()
	{
		auto LoadValue = [](const TCHAR* Key, const FString& Fallback)
		{
			FString Value;
			return GConfig->GetString(
				SettingsSection, Key, Value, GEditorPerProjectIni)
				? Value : Fallback;
		};
		Images->SetText(FText::FromString(LoadValue(TEXT("Images"), {})));
		Geometry->SetText(FText::FromString(LoadValue(TEXT("Geometry"), {})));
		Audio->SetText(FText::FromString(LoadValue(TEXT("Audio"), {})));
		Output->SetText(FText::FromString(LoadValue(
			TEXT("Output"),
			FPaths::ProjectContentDir() / TEXT("openvolumetric.mp4"))));
		AdaptiveOutputFolder->SetText(FText::FromString(LoadValue(
			TEXT("AdaptiveOutputFolder"), FPaths::ProjectContentDir())));
		PresentationName->SetText(FText::FromString(LoadValue(
			TEXT("PresentationName"), TEXT("openvolumetric"))));
		FFmpeg->SetText(FText::FromString(LoadValue(
			TEXT("FFmpeg"), FindFFmpeg())));
		GConfig->GetBool(
			SettingsSection,
			TEXT("GeometryCompression"),
			bGeometryCompression,
			GEditorPerProjectIni);
		GConfig->GetBool(
			SettingsSection,
			TEXT("LimitGeometryKeyframeInterval"),
			bLimitGeometryKeyframeInterval,
			GEditorPerProjectIni);
		MaximumGeometryFrames->SetText(FText::FromString(LoadValue(
			TEXT("MaximumGeometryKeyframeInterval"), TEXT("60"))));
		GConfig->GetBool(
			SettingsSection,
			TEXT("FragmentedMp4"),
			bFragmentedMp4,
			GEditorPerProjectIni);
		GConfig->GetBool(
			SettingsSection,
			TEXT("AdaptivePackage"),
			bAdaptivePackage,
			GEditorPerProjectIni);
		if (bAdaptivePackage)
		{
			bFragmentedMp4 = true;
		}
		FragmentDuration->SetText(FText::FromString(LoadValue(
			TEXT("FragmentDurationSeconds"), TEXT("2"))));
	}

	void Save() const
	{
		GConfig->SetString(
			SettingsSection, TEXT("Images"),
			*Images->GetText().ToString(), GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection, TEXT("Geometry"),
			*Geometry->GetText().ToString(), GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection, TEXT("Audio"),
			*Audio->GetText().ToString(), GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection, TEXT("Output"),
			*Output->GetText().ToString(), GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection, TEXT("AdaptiveOutputFolder"),
			*AdaptiveOutputFolder->GetText().ToString(), GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection, TEXT("PresentationName"),
			*PresentationName->GetText().ToString(), GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection, TEXT("FFmpeg"),
			*FFmpeg->GetText().ToString(), GEditorPerProjectIni);
		GConfig->SetBool(
			SettingsSection,
			TEXT("GeometryCompression"),
			bGeometryCompression,
			GEditorPerProjectIni);
		GConfig->SetBool(
			SettingsSection,
			TEXT("LimitGeometryKeyframeInterval"),
			bLimitGeometryKeyframeInterval,
			GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection,
			TEXT("MaximumGeometryKeyframeInterval"),
			*MaximumGeometryFrames->GetText().ToString(),
			GEditorPerProjectIni);
		GConfig->SetBool(
			SettingsSection,
			TEXT("FragmentedMp4"),
			bFragmentedMp4,
			GEditorPerProjectIni);
		GConfig->SetBool(
			SettingsSection,
			TEXT("AdaptivePackage"),
			bAdaptivePackage,
			GEditorPerProjectIni);
		GConfig->SetString(
			SettingsSection,
			TEXT("FragmentDurationSeconds"),
			*FragmentDuration->GetText().ToString(),
			GEditorPerProjectIni);
		GConfig->Flush(false, GEditorPerProjectIni);
	}

	void BrowseDirectory(const TSharedPtr<SEditableTextBox>& Field)
	{
		FString Selected;
		if (FDesktopPlatformModule::Get()->OpenDirectoryDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(
				nullptr),
			TEXT("Choose sequence directory"),
			Field->GetText().ToString(),
			Selected))
		{
			Field->SetText(FText::FromString(Selected));
		}
	}

	void BrowseFile(
		const TSharedPtr<SEditableTextBox>& Field,
		const FString& Title,
		const FString& Filter)
	{
		TArray<FString> Selected;
		if (FDesktopPlatformModule::Get()->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(
				nullptr),
			Title,
			FPaths::GetPath(Field->GetText().ToString()),
			TEXT(""),
			Filter,
			EFileDialogFlags::None,
			Selected) &&
			!Selected.IsEmpty())
		{
			Field->SetText(FText::FromString(Selected[0]));
		}
	}

	void BrowseOutput()
	{
		TArray<FString> Selected;
		if (FDesktopPlatformModule::Get()->SaveFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(
				nullptr),
			TEXT("Output OpenVolumetric MP4"),
			FPaths::GetPath(Output->GetText().ToString()),
			TEXT("openvolumetric.mp4"),
			TEXT("MP4 video (*.mp4)|*.mp4"),
			EFileDialogFlags::None,
			Selected) &&
			!Selected.IsEmpty())
		{
			Output->SetText(FText::FromString(Selected[0]));
		}
	}

	bool ValidateCurrent(FEncodingInputs& Inputs, FString& Error) const
	{
		return Validate(
			Images->GetText().ToString(),
			Geometry->GetText().ToString(),
			Audio->GetText().ToString(),
			Output->GetText().ToString(),
			FFmpeg->GetText().ToString(),
			FCString::Atod(*FrameRate->GetText().ToString()),
			bOverwrite,
			bAdaptivePackage,
			Inputs,
			Error);
	}

	void ValidateAndReport()
	{
		FEncodingInputs Inputs;
		FString Error;
		if (!ValidateCurrent(Inputs, Error))
		{
			State->SetStatus(Error);
			State->Append(TEXT("Validation failed: ") + Error);
			return;
		}
		const FString Message = FString::Printf(
			TEXT("Valid: %d matched frames (%d-%d)."),
			Inputs.Images.Num(),
			Inputs.Images[0].Frame,
			Inputs.Images.Last().Frame);
		State->SetStatus(Message);
		State->Append(Message);
	}

	void Start()
	{
		if (State->bRunning.Load())
		{
			return;
		}
		FEncodingInputs Inputs;
		FString Error;
		if (!ValidateCurrent(Inputs, Error))
		{
			State->SetStatus(Error);
			State->Append(TEXT("Cannot encode: ") + Error);
			return;
		}
		Save();

		const FString ImageDirectory = Images->GetText().ToString();
		const FString AudioFile = Audio->GetText().ToString();
		const FString OutputFile = Output->GetText().ToString();
		const FString FFmpegPath = FFmpeg->GetText().ToString();
		const double FPS =
			FCString::Atod(*FrameRate->GetText().ToString());
		const int32 FragmentDurationSeconds = bFragmentedMp4
			? FCString::Atoi(*FragmentDuration->GetText().ToString())
			: 0;
		const double ExactFragmentFrames =
			FPS * static_cast<double>(FragmentDurationSeconds);
		const int32 FragmentFrameInterval =
			FMath::RoundToInt(ExactFragmentFrames);
		if (bFragmentedMp4 &&
			((FragmentDurationSeconds != 1 &&
			  FragmentDurationSeconds != 2 &&
			  FragmentDurationSeconds != 4) ||
			 FragmentFrameInterval <= 0 ||
			 !FMath::IsNearlyEqual(
				 ExactFragmentFrames,
				 static_cast<double>(FragmentFrameInterval),
				 1.0e-6)))
		{
			const FString Message = TEXT(
				"Fragment duration must be 1, 2, or 4 seconds and contain an integral number of source frames.");
			State->SetStatus(Message);
			State->Append(TEXT("Cannot encode: ") + Message);
			return;
		}
		const FEncodingSettings Settings = GetPreset(Preset);
		TArray<FEncodingSettings> RepresentationSettings;
		TArray<FString> RepresentationOutputs;
		TArray<FString> RepresentationIds;
		FString ManifestOutput;
		FString AdaptivePresentationId;
		if (bAdaptivePackage)
		{
			std::vector<openvolumetric::authoring::AdaptiveLadderEntry> Ladder;
			std::string NativeError;
			if (!openvolumetric::authoring::adaptive_ladder_settings(
				GetNativePreset(Preset),
				FragmentDurationSeconds,
				Ladder,
				NativeError))
			{
				const FString Message = UTF8_TO_TCHAR(NativeError.c_str());
				State->SetStatus(Message);
				State->Append(TEXT("Cannot encode: ") + Message);
				return;
			}
			AdaptivePresentationId =
				PresentationName->GetText().ToString().TrimStartAndEnd();
			const FString PackageDirectory =
				FPaths::ConvertRelativePathToFull(
					AdaptiveOutputFolder->GetText().ToString()) /
				AdaptivePresentationId;
			if (AdaptivePresentationId.IsEmpty())
			{
				State->SetStatus(TEXT("Choose a presentation name."));
				return;
			}
			for (int32 LadderIndex = 0;
				LadderIndex < static_cast<int32>(Ladder.size());
				++LadderIndex)
			{
				const auto& Entry = Ladder[LadderIndex];
				RepresentationSettings.Add(ToUnrealSettings(Entry.settings));
				RepresentationIds.Add(UTF8_TO_TCHAR(Entry.id.c_str()));
				RepresentationOutputs.Add(PackageDirectory /
					(LadderIndex == 0 ? TEXT("low.mp4") : TEXT("high.mp4")));
			}
			ManifestOutput = PackageDirectory / TEXT("manifest.json");
			for (const FString& Path : RepresentationOutputs)
			{
				if (IFileManager::Get().FileExists(*Path) && !bOverwrite)
				{
					State->SetStatus(TEXT("An adaptive output exists; enable overwrite."));
					return;
				}
			}
			if (IFileManager::Get().FileExists(*ManifestOutput) && !bOverwrite)
			{
				State->SetStatus(TEXT("The adaptive manifest exists; enable overwrite."));
				return;
			}
		}
		else
		{
			RepresentationSettings.Add(Settings);
			RepresentationOutputs.Add(OutputFile);
			RepresentationIds.Add(TEXT("single"));
		}
		const bool bReplace = bOverwrite;
		const bool bCompressGeometry = bGeometryCompression;
		const int32 MaximumGeometryKeyframeInterval =
			bCompressGeometry &&
				(Settings.GeometryKeyframeInterval > 0 ||
				 bLimitGeometryKeyframeInterval)
				? (Settings.GeometryKeyframeInterval > 0
					? Settings.GeometryKeyframeInterval
					: FCString::Atoi(
						*MaximumGeometryFrames->GetText().ToString()))
				: 0;
		if (MaximumGeometryKeyframeInterval < 0 ||
			(bCompressGeometry && bLimitGeometryKeyframeInterval &&
				MaximumGeometryKeyframeInterval == 0))
		{
			const FString Message =
				TEXT("Maximum geometry frames must be at least one.");
			State->SetStatus(Message);
			State->Append(TEXT("Cannot encode: ") + Message);
			return;
		}
		const TSharedRef<FAuthoringState, ESPMode::ThreadSafe> Job = State;

		Job->bCancel.Store(false);
		Job->bRunning.Store(true);
		Job->Progress.Store(0.0f);
		{
			FScopeLock Lock(&Job->TextMutex);
			Job->Log.Empty();
			Job->Status = TEXT("Starting");
		}

		Async(EAsyncExecution::ThreadPool,
			[Job, Inputs = MoveTemp(Inputs), ImageDirectory, AudioFile,
			 OutputFile, FFmpegPath, FPS,
			 RepresentationSettings = MoveTemp(RepresentationSettings),
			 RepresentationOutputs = MoveTemp(RepresentationOutputs),
			 RepresentationIds = MoveTemp(RepresentationIds),
			 ManifestOutput, AdaptivePresentationId,
			 bAdaptivePackage = bAdaptivePackage, bReplace,
			 bCompressGeometry, MaximumGeometryKeyframeInterval,
			 FragmentDurationSeconds, FragmentFrameInterval]
			{
				bool bSuccess = true;
				FString Failure;
				std::vector<openvolumetric::authoring::AdaptivePackageRepresentation>
					AdaptiveInputs;
				for (int32 RepresentationIndex = 0;
					RepresentationIndex < RepresentationSettings.Num();
					++RepresentationIndex)
				{
					const FEncodingSettings& ActiveSettings =
						RepresentationSettings[RepresentationIndex];
					const FString& ActiveOutput =
						RepresentationOutputs[RepresentationIndex];
					Job->Append(FString::Printf(
						TEXT("Encoding representation %d/%d: %s"),
						RepresentationIndex + 1,
						RepresentationSettings.Num(),
						*FPaths::GetCleanFilename(ActiveOutput)));
				const FString TempDirectory =
					FPaths::ProjectIntermediateDir() /
					TEXT("OpenVolumetricAuthoring") /
					FGuid::NewGuid().ToString(EGuidFormats::Digits);
				const FString DracoDirectory =
					TempDirectory / TEXT("geometry");
				const FString MediaPath =
					TempDirectory / TEXT("media.mp4");
				const FString PackagedPath =
					TempDirectory / TEXT("packaged.mp4");
				IFileManager::Get().MakeDirectory(
					*DracoDirectory, true);

				bool bRepresentationSuccess = false;
				std::uint64_t GeometryPayloadBytes = 0;
				Job->Append(FString::Printf(
					TEXT("Encoding %d OBJ frames with Draco."),
					Inputs.Geometry.Num()));
				for (int32 Index = 0;
					Index < Inputs.Geometry.Num() &&
					!Job->bCancel.Load();
					++Index)
				{
					const FNumberedFile& Source = Inputs.Geometry[Index];
					const FString Destination =
						DracoDirectory / Source.Stem + TEXT(".drc");
					openvolumetric::authoring::DracoEncodeOptions Options;
					Options.position_quantization = ActiveSettings.PositionBits;
					Options.normal_quantization = ActiveSettings.NormalBits;
					Options.texture_quantization = ActiveSettings.UVBits;
					Options.encode_speed = ActiveSettings.EncodeSpeed;
					Options.decode_speed = ActiveSettings.DecodeSpeed;
					Options.preserve_point_order = bCompressGeometry;
					std::string NativeError;
					if (!openvolumetric::authoring::encode_obj_to_draco(
						std::filesystem::path(
							TCHAR_TO_UTF8(*Source.Path)),
						std::filesystem::path(
							TCHAR_TO_UTF8(*Destination)),
						Options,
						NativeError))
					{
						Failure = UTF8_TO_TCHAR(NativeError.c_str());
						break;
					}
					Job->Progress.Store(
						0.45f * static_cast<float>(Index + 1) /
						Inputs.Geometry.Num());
					Job->SetStatus(FString::Printf(
						TEXT("Encoding geometry %d/%d"),
						Index + 1,
						Inputs.Geometry.Num()));
				}

				if (Failure.IsEmpty() && !Job->bCancel.Load())
				{
					Job->SetStatus(TEXT("Encoding video and audio"));
					Job->Progress.Store(0.48f);
					const FNumberedFile& First = Inputs.Images[0];
					const FString Pattern = ImageDirectory /
						FString::Printf(
							TEXT("%%0%dd%s"),
							First.Stem.Len(),
							*First.Extension);
					openvolumetric::authoring::MediaEncodeRequest Request;
					Request.image_pattern =
						std::filesystem::path(TCHAR_TO_UTF8(*Pattern));
					if (!AudioFile.IsEmpty())
						Request.audio_path =
							std::filesystem::path(TCHAR_TO_UTF8(*AudioFile));
					Request.output_path =
						std::filesystem::path(TCHAR_TO_UTF8(*MediaPath));
					Request.frame_rate = FPS;
					Request.first_frame = First.Frame;
					Request.frame_count = Inputs.Images.Num();
					Request.settings.codec =
						ActiveSettings.Codec == TEXT("libx265")
							? openvolumetric::authoring::VideoCodec::HEVC
							: openvolumetric::authoring::VideoCodec::H264;
					Request.settings.crf = ActiveSettings.Crf;
					Request.settings.video_keyframe_interval =
						ActiveSettings.Keyframes;
					Request.settings.reference_frames = ActiveSettings.References;
					Request.settings.disable_sao = ActiveSettings.bDisableSao;
					Request.settings.maximum_video_bitrate_kbps =
						ActiveSettings.MaximumVideoBitrateKbps;
					Request.settings.video_buffer_size_kbps =
						ActiveSettings.VideoBufferSizeKbps;
					Request.settings.geometry_keyframe_interval =
						ActiveSettings.GeometryKeyframeInterval;
					Request.settings.fragment_duration_seconds =
						FragmentDurationSeconds;
					std::vector<std::string> NativeArguments;
					std::string ArgumentError;
					if (!openvolumetric::authoring::build_ffmpeg_arguments(
						Request, NativeArguments, ArgumentError))
					{
						Failure = UTF8_TO_TCHAR(ArgumentError.c_str());
					}
					else
					{
						FString Args;
						for (const std::string& Argument : NativeArguments)
						{
							if (!Args.IsEmpty())
								Args += TEXT(" ");
							Args += Quote(UTF8_TO_TCHAR(Argument.c_str()));
						}
						Job->Append(
							TEXT("> ") + FFmpegPath + TEXT(" ") + Args);

						int32 ReturnCode = -1;
						FString StdOut;
						FString StdErr;
						if (!FPlatformProcess::ExecProcess(
							*FFmpegPath,
							*Args,
							&ReturnCode,
							&StdOut,
							&StdErr) ||
							ReturnCode != 0)
						{
							Job->Append(StdOut);
							Job->Append(StdErr);
							Failure = FString::Printf(
								TEXT("FFmpeg failed with exit code %d."),
								ReturnCode);
						}
					}
				}

				if (Failure.IsEmpty() && !Job->bCancel.Load())
				{
					Job->SetStatus(TEXT("Packaging and verifying MP4"));
					Job->Progress.Store(0.78f);
					openvolumetric::authoring::PackOptions Options;
					Options.media_path = std::filesystem::path(
						TCHAR_TO_UTF8(*MediaPath));
					Options.geometry_directory = std::filesystem::path(
						TCHAR_TO_UTF8(*DracoDirectory));
					Options.source_geometry_directory =
						std::filesystem::path(
							TCHAR_TO_UTF8(
								*FPaths::GetPath(Inputs.Geometry[0].Path)));
					Options.output_path = std::filesystem::path(
						TCHAR_TO_UTF8(*PackagedPath));
					Options.enable_topology_compression =
						bCompressGeometry;
					Options.maximum_geometry_keyframe_interval =
						static_cast<std::uint32_t>(
							MaximumGeometryKeyframeInterval);
					Options.fragment_duration_seconds =
						static_cast<std::uint32_t>(
							FragmentDurationSeconds);
					Options.fragment_frame_interval =
						static_cast<std::uint32_t>(FragmentFrameInterval);
					Options.draco_options.position_quantization =
						ActiveSettings.PositionBits;
					Options.draco_options.normal_quantization =
						ActiveSettings.NormalBits;
					Options.draco_options.texture_quantization =
						ActiveSettings.UVBits;
					Options.draco_options.encode_speed =
						ActiveSettings.EncodeSpeed;
					Options.draco_options.decode_speed =
						ActiveSettings.DecodeSpeed;
					openvolumetric::authoring::PackStatistics Statistics;
					if (!openvolumetric::authoring::pack_openvolumetric(
						Options, &Statistics))
					{
						Failure =
							TEXT("Native packaging or verification failed.");
					}
					else
					{
						GeometryPayloadBytes =
							Statistics.authored_payload_bytes +
							Statistics.packet_header_bytes;
						const double Reduction =
							Statistics.independent_payload_bytes == 0
								? 0.0
								: 100.0 * (1.0 -
									static_cast<double>(
										Statistics.authored_payload_bytes) /
									static_cast<double>(
										Statistics.independent_payload_bytes));
						Job->Append(FString::Printf(
							TEXT("Geometry statistics: %llu frames, %llu ")
							TEXT("independent mesh keyframes, %llu position ")
							TEXT("updates.\nGeometry payload: %llu bytes + ")
							TEXT("%llu bytes packet headers; independent ")
							TEXT("baseline %llu bytes; %.2f%% payload reduction."),
							static_cast<unsigned long long>(
								Statistics.frame_count),
							static_cast<unsigned long long>(
								Statistics.independent_mesh_count),
							static_cast<unsigned long long>(
								Statistics.position_update_count),
							static_cast<unsigned long long>(
								Statistics.authored_payload_bytes),
							static_cast<unsigned long long>(
								Statistics.packet_header_bytes),
							static_cast<unsigned long long>(
								Statistics.independent_payload_bytes),
							Reduction));
						if (Statistics.fragment_count > 0)
						{
							Job->Append(FString::Printf(
								TEXT("Fragmented MP4: %llu fragments at %d seconds."),
								static_cast<unsigned long long>(
									Statistics.fragment_count),
								FragmentDurationSeconds));
						}
					}
				}

				if (Failure.IsEmpty() && !Job->bCancel.Load())
				{
					IFileManager::Get().MakeDirectory(
						*FPaths::GetPath(ActiveOutput), true);
					if (bReplace)
					{
						IFileManager::Get().Delete(*ActiveOutput);
					}
					if (!IFileManager::Get().Move(
						*ActiveOutput, *PackagedPath, true, true))
					{
						Failure = TEXT("Could not move the completed MP4.");
					}
					else
					{
						bRepresentationSuccess = true;
						if (bAdaptivePackage)
						{
							openvolumetric::authoring::AdaptivePackageRepresentation Input;
							Input.id = TCHAR_TO_UTF8(
								*RepresentationIds[RepresentationIndex]);
							Input.resource_path = std::filesystem::path(
								TCHAR_TO_UTF8(*ActiveOutput));
							Input.compatibility_group = TCHAR_TO_UTF8(
								*(AdaptivePresentationId +
									TEXT("-coupled-v1")));
							Input.geometry_payload_bytes = GeometryPayloadBytes;
							Input.position_quantization_bits =
								static_cast<std::uint32_t>(ActiveSettings.PositionBits);
							Input.temporal_compression = bCompressGeometry;
							AdaptiveInputs.push_back(std::move(Input));
						}
					}
				}

				IFileManager::Get().DeleteDirectory(
					*TempDirectory, false, true);
				if (!bRepresentationSuccess)
				{
					bSuccess = false;
					break;
				}
				Job->Append(TEXT("Created ") + ActiveOutput);
				}

				if (bSuccess && bAdaptivePackage && !Job->bCancel.Load())
				{
					Job->SetStatus(TEXT("Writing adaptive manifest"));
					std::string NativeError;
					openvolumetric::authoring::AdaptivePackageOptions Options;
					Options.presentation_id = TCHAR_TO_UTF8(
						*AdaptivePresentationId);
					Options.manifest_path = std::filesystem::path(
						TCHAR_TO_UTF8(*ManifestOutput));
					Options.segment_duration_seconds = FragmentDurationSeconds;
					Options.representations = std::move(AdaptiveInputs);
					// The shared verifier must pass before the manifest is published.
					openvolumetric::authoring::AdaptivePackageVerification Verification;
					if (!openvolumetric::authoring::write_adaptive_package_manifest(
						Options, NativeError, &Verification))
					{
						Failure = UTF8_TO_TCHAR(NativeError.c_str());
						bSuccess = false;
					}
					else
					{
						Job->Append(UTF8_TO_TCHAR(
							Verification.summary().c_str()));
						Job->Append(TEXT("Created ") + ManifestOutput);
					}
				}
				if (Job->bCancel.Load())
				{
					Job->SetStatus(TEXT("Encoding cancelled"));
					Job->Append(TEXT("Encoding cancelled."));
				}
				else if (!bSuccess)
				{
					Job->SetStatus(TEXT("Encoding failed"));
					Job->Append(Failure);
				}
				else
				{
					Job->Progress.Store(1.0f);
					Job->SetStatus(TEXT("Encoding complete"));
					Job->Append(bAdaptivePackage
						? TEXT("Adaptive package complete.")
						: TEXT("Encoding complete."));
				}
				Job->bRunning.Store(false);
			});
	}
};

void SOpenVolumetricEncoderWindow::Construct(const FArguments&)
{
	Impl = new FImpl();
	auto PathRow =
		[this](
			const FText& Label,
			TSharedPtr<SEditableTextBox>& Field,
			TFunction<void()> Browse)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SBox).WidthOverride(125)
					[
						SNew(STextBlock).Text(Label)
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SAssignNew(Field, SEditableTextBox)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0)
				[
					SNew(SButton)
					.Text(NSLOCTEXT("OpenVolumetricAuthoring", "Browse", "Browse"))
					.OnClicked_Lambda(
						[Browse]
						{
							Browse();
							return FReply::Handled();
						})
				];
		};

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		.Padding(14)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT(
					"OpenVolumetricAuthoring",
					"Description",
					"Create one OpenVolumetric MP4 from matching numbered images "
					"and OBJ meshes, with optional audio."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				PathRow(
					NSLOCTEXT("OpenVolumetricAuthoring", "Images", "Image Sequence"),
					Impl->Images,
					[this] { Impl->BrowseDirectory(Impl->Images); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				PathRow(
					NSLOCTEXT("OpenVolumetricAuthoring", "Geometry", "OBJ Sequence"),
					Impl->Geometry,
					[this] { Impl->BrowseDirectory(Impl->Geometry); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				PathRow(
					NSLOCTEXT("OpenVolumetricAuthoring", "Audio", "Audio (optional)"),
					Impl->Audio,
					[this]
					{
						Impl->BrowseFile(
							Impl->Audio,
							TEXT("Choose audio"),
							TEXT("Audio|*.wav;*.mp3;*.m4a;*.aac;*.flac|All files|*.*"));
					})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SBox)
				.Visibility_Lambda(
					[this]
					{
						return Impl->bAdaptivePackage
							? EVisibility::Collapsed
							: EVisibility::Visible;
					})
				[
					PathRow(
						NSLOCTEXT("OpenVolumetricAuthoring", "Output", "Output MP4"),
						Impl->Output,
						[this] { Impl->BrowseOutput(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SBox)
				.Visibility_Lambda(
					[this]
					{
						return Impl->bAdaptivePackage
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
				[
					PathRow(
						NSLOCTEXT(
							"OpenVolumetricAuthoring",
							"AdaptiveOutputFolder",
							"Output Parent Folder"),
						Impl->AdaptiveOutputFolder,
						[this]
						{
							Impl->BrowseDirectory(Impl->AdaptiveOutputFolder);
						})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda(
					[this]
					{
						return Impl->bAdaptivePackage
							? EVisibility::Visible
							: EVisibility::Collapsed;
					})
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0, 0, 8, 0)
				[
					SNew(SBox).WidthOverride(125)
					[
						SNew(STextBlock).Text(NSLOCTEXT(
							"OpenVolumetricAuthoring",
							"PresentationName",
							"Presentation Name"))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SAssignNew(Impl->PresentationName, SEditableTextBox)
					.ToolTipText(NSLOCTEXT(
						"OpenVolumetricAuthoring",
						"PresentationNameTooltip",
						"Creates a package folder with this name containing manifest.json, low.mp4, and high.mp4."))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(133)
					[
						SNew(STextBlock).Text(NSLOCTEXT(
							"OpenVolumetricAuthoring", "Preset", "Platform Preset"))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SComboBox<TSharedPtr<FString>>)
					.OptionsSource(&Impl->PresetNames)
					.InitiallySelectedItem(Impl->SelectedPreset)
					.OnGenerateWidget_Lambda(
						[](TSharedPtr<FString> Item)
						{
							return SNew(STextBlock)
								.Text(FText::FromString(*Item));
						})
					.OnSelectionChanged_Lambda(
						[this](TSharedPtr<FString> Item, ESelectInfo::Type)
						{
							Impl->SelectedPreset = Item;
							const int32 Index =
								Impl->PresetNames.IndexOfByKey(Item);
							Impl->Preset = static_cast<EOpenVolumetricPreset>(
								FMath::Max(0, Index));
						})
					[
						SNew(STextBlock)
						.Text_Lambda(
							[this]
							{
								return FText::FromString(
									*Impl->SelectedPreset);
							})
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(133)
					[
						SNew(STextBlock).Text(NSLOCTEXT(
							"OpenVolumetricAuthoring", "FrameRate", "Frame Rate"))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SAssignNew(Impl->FrameRate, SEditableTextBox)
						.Text(FText::FromString(TEXT("30")))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				PathRow(
					NSLOCTEXT("OpenVolumetricAuthoring", "FFmpeg", "FFmpeg"),
					Impl->FFmpeg,
					[this]
					{
						Impl->BrowseFile(
							Impl->FFmpeg,
							TEXT("Choose FFmpeg"),
							TEXT("All files|*.*"));
					})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda(
						[this]
						{
							return Impl->bAdaptivePackage
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						})
					.ToolTipText(NSLOCTEXT(
						"OpenVolumetricAuthoring",
						"AdaptivePackageTooltip",
						"Encode aligned low/high fragmented representations and an OpenVolumetric adaptive manifest. Requires a streaming preset."))
					.OnCheckStateChanged_Lambda(
						[this](ECheckBoxState State)
						{
							Impl->bAdaptivePackage =
								State == ECheckBoxState::Checked;
							if (Impl->bAdaptivePackage)
							{
								Impl->bFragmentedMp4 = true;
							}
						})
				[
					SNew(STextBlock)
						.Text(NSLOCTEXT(
							"OpenVolumetricAuthoring",
							"AdaptivePackage",
							"Adaptive Package (low/high + manifest)"))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SCheckBox)
					.IsEnabled_Lambda(
						[this] { return !Impl->bAdaptivePackage; })
					.IsChecked_Lambda(
						[this]
						{
							return Impl->bFragmentedMp4
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						})
					.ToolTipText(NSLOCTEXT(
						"OpenVolumetricAuthoring",
						"FragmentedMp4Tooltip",
						"Write aligned independently addressable MP4 fragments with full video and geometry access points."))
					.OnCheckStateChanged_Lambda(
						[this](ECheckBoxState State)
						{
							if (!Impl->bAdaptivePackage)
							{
								Impl->bFragmentedMp4 =
									State == ECheckBoxState::Checked;
							}
						})
				[
					SNew(STextBlock)
						.Text(NSLOCTEXT(
							"OpenVolumetricAuthoring",
							"FragmentedMp4",
							"Fragmented MP4"))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(180)
					[
						SNew(STextBlock).Text(NSLOCTEXT(
							"OpenVolumetricAuthoring",
							"FragmentDuration",
							"Fragment Duration (1/2/4 s)"))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SAssignNew(Impl->FragmentDuration, SEditableTextBox)
						.Text(FText::FromString(TEXT("2")))
						.IsEnabled_Lambda(
							[this] { return Impl->bFragmentedMp4; })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda(
						[this]
						{
							return Impl->bGeometryCompression
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						})
					.ToolTipText(NSLOCTEXT(
						"OpenVolumetricAuthoring",
						"GeometryCompressionTooltip",
						"Reuse matching topology with position-only Draco updates. Disable to encode every packet as an independent Draco mesh."))
					.OnCheckStateChanged_Lambda(
						[this](ECheckBoxState State)
						{
							Impl->bGeometryCompression =
								State == ECheckBoxState::Checked;
						})
					[
						SNew(STextBlock)
							.Text(NSLOCTEXT(
								"OpenVolumetricAuthoring",
								"GeometryCompression",
								"Geometry Compression"))
					]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SCheckBox)
					.IsEnabled_Lambda(
						[this] { return Impl->bGeometryCompression; })
					.IsChecked_Lambda(
						[this]
						{
							return Impl->bLimitGeometryKeyframeInterval
								? ECheckBoxState::Checked
								: ECheckBoxState::Unchecked;
						})
					.ToolTipText(NSLOCTEXT(
						"OpenVolumetricAuthoring",
						"LimitGeometryKeyframesTooltip",
						"Force periodic full Draco reference meshes to bound geometry seek and streaming preroll."))
					.OnCheckStateChanged_Lambda(
						[this](ECheckBoxState State)
						{
							Impl->bLimitGeometryKeyframeInterval =
								State == ECheckBoxState::Checked;
						})
					[
						SNew(STextBlock)
							.Text(NSLOCTEXT(
								"OpenVolumetricAuthoring",
								"LimitGeometryKeyframes",
								"Limit Geometry Keyframes"))
					]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(180)
					[
						SNew(STextBlock).Text(NSLOCTEXT(
							"OpenVolumetricAuthoring",
							"MaximumGeometryFrames",
							"Maximum Geometry Frames"))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SAssignNew(
						Impl->MaximumGeometryFrames,
						SEditableTextBox)
						.Text(FText::FromString(TEXT("60")))
						.IsEnabled_Lambda(
							[this]
							{
								return Impl->bGeometryCompression &&
									Impl->bLimitGeometryKeyframeInterval;
							})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SButton)
					.Text_Lambda(
						[this]
						{
							return FText::FromString(
								Impl->bOverwrite
									? TEXT("Overwrite Output: Yes")
									: TEXT("Overwrite Output: No"));
						})
					.OnClicked_Lambda(
						[this]
						{
							Impl->bOverwrite = !Impl->bOverwrite;
							return FReply::Handled();
						})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 4, 0)
				[
					SNew(SButton)
						.Text(NSLOCTEXT(
							"OpenVolumetricAuthoring", "Validate", "Validate Inputs"))
						.IsEnabled_Lambda(
							[this] { return !Impl->State->bRunning.Load(); })
						.OnClicked_Lambda(
							[this]
							{
								Impl->ValidateAndReport();
								return FReply::Handled();
							})
				]
				+ SHorizontalBox::Slot().FillWidth(1).Padding(4, 0, 0, 0)
				[
					SNew(SButton)
						.Text(NSLOCTEXT(
							"OpenVolumetricAuthoring", "Encode", "Encode OpenVolumetric MP4"))
						.IsEnabled_Lambda(
							[this] { return !Impl->State->bRunning.Load(); })
						.OnClicked_Lambda(
							[this]
							{
								Impl->Start();
								return FReply::Handled();
							})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)
			[
				SNew(SProgressBar)
					.Percent_Lambda(
						[this]
						{
							return TOptional<float>(
								Impl->State->Progress.Load());
						})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(STextBlock)
					.Text_Lambda(
						[this]
						{
							return FText::FromString(
								Impl->State->GetStatus());
						})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				SNew(SButton)
					.Text(NSLOCTEXT("OpenVolumetricAuthoring", "Cancel", "Cancel"))
					.Visibility_Lambda(
						[this]
						{
							return Impl->State->bRunning.Load()
								? EVisibility::Visible
								: EVisibility::Collapsed;
						})
					.OnClicked_Lambda(
						[this]
						{
							Impl->State->bCancel.Store(true);
							Impl->State->SetStatus(TEXT("Cancelling"));
							return FReply::Handled();
						})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)
			[
				SNew(STextBlock)
					.Text(NSLOCTEXT(
						"OpenVolumetricAuthoring", "LogLabel", "Encoder Log"))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).MinDesiredHeight(180)
				[
					SNew(SMultiLineEditableTextBox)
						.IsReadOnly(true)
						.Text_Lambda(
							[this]
							{
								return FText::FromString(
									Impl->State->GetLog());
							})
				]
			]
		]
	];

	Impl->Load();
	RegisterActiveTimer(
		0.1f,
		FWidgetActiveTimerDelegate::CreateLambda(
			[](double, float)
			{
				return EActiveTimerReturnType::Continue;
			}));
}

SOpenVolumetricEncoderWindow::~SOpenVolumetricEncoderWindow()
{
	if (Impl)
	{
		Impl->Save();
		Impl->State->bCancel.Store(true);
		delete Impl;
		Impl = nullptr;
	}
}
