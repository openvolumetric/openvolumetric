#include "OpenVolEncoderWindow.h"

#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "DracoMeshEncoder.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "VolumetricVideoPacker.h"
#include "Widgets/Input/SButton.h"
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
const TCHAR* SettingsSection = TEXT("OpenVol.Authoring");

enum class EOpenVolPreset : uint8
{
	DesktopQuality,
	QuestBalanced,
	QuestPerformance
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
};

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

FEncodingSettings GetPreset(EOpenVolPreset Preset)
{
	switch (Preset)
	{
	case EOpenVolPreset::DesktopQuality:
		return {
			TEXT("libx265"), 20, 60, 3, false,
			14, 10, 12, 5, 5};
	case EOpenVolPreset::QuestPerformance:
		return {
			TEXT("libx264"), 23, 30, 1, false,
			12, 8, 10, 8, 10};
	default:
		return {
			TEXT("libx265"), 25, 30, 1, true,
			14, 10, 12, 7, 9};
	}
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
	if (OutputFile.IsEmpty())
	{
		OutError = TEXT("Choose an output MP4.");
		return false;
	}
	if (IFileManager::Get().FileExists(*OutputFile) && !bOverwrite)
	{
		OutError =
			TEXT("The output exists. Enable overwrite or choose another path.");
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
	if (OutInputs.Images.Num() != OutInputs.Geometry.Num())
	{
		OutError = TEXT("Image and OBJ sequence lengths do not match.");
		return false;
	}
	for (int32 Index = 0; Index < OutInputs.Images.Num(); ++Index)
	{
		if (OutInputs.Images[Index].Frame !=
			OutInputs.Geometry[Index].Frame)
		{
			OutError = TEXT("Image and OBJ frame numbers must match.");
			return false;
		}
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

class SOpenVolEncoderWindow::FImpl
{
public:
	TSharedRef<FAuthoringState, ESPMode::ThreadSafe> State =
		MakeShared<FAuthoringState, ESPMode::ThreadSafe>();
	EOpenVolPreset Preset = EOpenVolPreset::QuestBalanced;
	TArray<TSharedPtr<FString>> PresetNames;
	TSharedPtr<FString> SelectedPreset;
	TSharedPtr<SEditableTextBox> Images;
	TSharedPtr<SEditableTextBox> Geometry;
	TSharedPtr<SEditableTextBox> Audio;
	TSharedPtr<SEditableTextBox> Output;
	TSharedPtr<SEditableTextBox> FFmpeg;
	TSharedPtr<SEditableTextBox> FrameRate;
	bool bOverwrite = false;

	FImpl()
	{
		PresetNames = {
			MakeShared<FString>(TEXT("Desktop Quality")),
			MakeShared<FString>(TEXT("Quest Balanced")),
			MakeShared<FString>(TEXT("Quest Performance"))};
		SelectedPreset = PresetNames[1];
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
			FPaths::ProjectContentDir() / TEXT("openvol.mp4"))));
		FFmpeg->SetText(FText::FromString(LoadValue(
			TEXT("FFmpeg"), FindFFmpeg())));
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
			SettingsSection, TEXT("FFmpeg"),
			*FFmpeg->GetText().ToString(), GEditorPerProjectIni);
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
			TEXT("Output OpenVol MP4"),
			FPaths::GetPath(Output->GetText().ToString()),
			TEXT("openvol.mp4"),
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
		const FEncodingSettings Settings = GetPreset(Preset);
		const bool bReplace = bOverwrite;
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
			 OutputFile, FFmpegPath, FPS, Settings, bReplace]
			{
				const FString TempDirectory =
					FPaths::ProjectIntermediateDir() /
					TEXT("OpenVolAuthoring") /
					FGuid::NewGuid().ToString(EGuidFormats::Digits);
				const FString DracoDirectory =
					TempDirectory / TEXT("geometry");
				const FString MediaPath =
					TempDirectory / TEXT("media.mp4");
				const FString PackagedPath =
					TempDirectory / TEXT("packaged.mp4");
				IFileManager::Get().MakeDirectory(
					*DracoDirectory, true);

				bool bSuccess = false;
				FString Failure;
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
					openvol::authoring::DracoEncodeOptions Options;
					Options.position_quantization = Settings.PositionBits;
					Options.normal_quantization = Settings.NormalBits;
					Options.texture_quantization = Settings.UVBits;
					Options.encode_speed = Settings.EncodeSpeed;
					Options.decode_speed = Settings.DecodeSpeed;
					std::string NativeError;
					if (!openvol::authoring::encode_obj_to_draco(
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
					FString Args = FString::Printf(
						TEXT("-hide_banner -y -framerate %.8g ")
						TEXT("-start_number %d -i %s "),
						FPS,
						First.Frame,
						*Quote(Pattern));
					if (!AudioFile.IsEmpty())
					{
						Args += TEXT("-i ") + Quote(AudioFile) + TEXT(" ");
					}
					Args += FString::Printf(
						TEXT("-frames:v %d -c:v %s -crf %d ")
						TEXT("-pix_fmt yuv420p "),
						Inputs.Images.Num(),
						*Settings.Codec,
						Settings.Crf);
					if (Settings.Codec == TEXT("libx265"))
					{
						Args += FString::Printf(
							TEXT("-x265-params ")
							TEXT("\"keyint=%d:min-keyint=1:bframes=0:ref=%d%s\" "),
							Settings.Keyframes,
							Settings.References,
							Settings.bDisableSao
								? TEXT(":no-sao=1") : TEXT(""));
					}
					else
					{
						Args += FString::Printf(
							TEXT("-preset fast -x264-params ")
							TEXT("\"keyint=%d:min-keyint=1:bframes=0:ref=%d\" "),
							Settings.Keyframes,
							Settings.References);
					}
					Args += AudioFile.IsEmpty()
						? TEXT("-an ")
						: TEXT("-c:a aac -b:a 192k -af apad -shortest ");
					Args += Quote(MediaPath);
					Job->Append(TEXT("> ") + FFmpegPath + TEXT(" ") + Args);

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

				if (Failure.IsEmpty() && !Job->bCancel.Load())
				{
					Job->SetStatus(TEXT("Packaging and verifying MP4"));
					Job->Progress.Store(0.78f);
					openvol::authoring::PackOptions Options;
					Options.media_path = std::filesystem::path(
						TCHAR_TO_UTF8(*MediaPath));
					Options.geometry_directory = std::filesystem::path(
						TCHAR_TO_UTF8(*DracoDirectory));
					Options.output_path = std::filesystem::path(
						TCHAR_TO_UTF8(*PackagedPath));
					if (!openvol::authoring::pack_openvol(Options))
					{
						Failure =
							TEXT("Native packaging or verification failed.");
					}
				}

				if (Failure.IsEmpty() && !Job->bCancel.Load())
				{
					IFileManager::Get().MakeDirectory(
						*FPaths::GetPath(OutputFile), true);
					if (bReplace)
					{
						IFileManager::Get().Delete(*OutputFile);
					}
					if (!IFileManager::Get().Move(
						*OutputFile, *PackagedPath, true, true))
					{
						Failure = TEXT("Could not move the completed MP4.");
					}
					else
					{
						bSuccess = true;
					}
				}

				IFileManager::Get().DeleteDirectory(
					*TempDirectory, false, true);
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
					Job->Append(TEXT("Created ") + OutputFile);
				}
				Job->bRunning.Store(false);
			});
	}
};

void SOpenVolEncoderWindow::Construct(const FArguments&)
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
					.Text(NSLOCTEXT("OpenVolAuthoring", "Browse", "Browse"))
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
					"OpenVolAuthoring",
					"Description",
					"Create one OpenVol MP4 from matching numbered images "
					"and OBJ meshes, with optional audio."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				PathRow(
					NSLOCTEXT("OpenVolAuthoring", "Images", "Image Sequence"),
					Impl->Images,
					[this] { Impl->BrowseDirectory(Impl->Images); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				PathRow(
					NSLOCTEXT("OpenVolAuthoring", "Geometry", "OBJ Sequence"),
					Impl->Geometry,
					[this] { Impl->BrowseDirectory(Impl->Geometry); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3)
			[
				PathRow(
					NSLOCTEXT("OpenVolAuthoring", "Audio", "Audio (optional)"),
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
				PathRow(
					NSLOCTEXT("OpenVolAuthoring", "Output", "Output MP4"),
					Impl->Output,
					[this] { Impl->BrowseOutput(); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 3)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(133)
					[
						SNew(STextBlock).Text(NSLOCTEXT(
							"OpenVolAuthoring", "Preset", "Platform Preset"))
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
							Impl->Preset = static_cast<EOpenVolPreset>(
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
							"OpenVolAuthoring", "FrameRate", "Frame Rate"))
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
					NSLOCTEXT("OpenVolAuthoring", "FFmpeg", "FFmpeg"),
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
							"OpenVolAuthoring", "Validate", "Validate Inputs"))
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
							"OpenVolAuthoring", "Encode", "Encode OpenVol MP4"))
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
					.Text(NSLOCTEXT("OpenVolAuthoring", "Cancel", "Cancel"))
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
						"OpenVolAuthoring", "LogLabel", "Encoder Log"))
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

SOpenVolEncoderWindow::~SOpenVolEncoderWindow()
{
	if (Impl)
	{
		Impl->Save();
		Impl->State->bCancel.Store(true);
		delete Impl;
		Impl = nullptr;
	}
}
