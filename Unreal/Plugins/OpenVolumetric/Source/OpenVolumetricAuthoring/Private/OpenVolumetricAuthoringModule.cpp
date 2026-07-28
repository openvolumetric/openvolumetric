#include "OpenVolumetricAuthoringModule.h"

#include "OpenVolumetricEncoderWindow.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

IMPLEMENT_MODULE(FOpenVolumetricAuthoringModule, OpenVolumetricAuthoring)

namespace
{
const FName OpenVolumetricEncoderTabName(TEXT("OpenVolumetricEncoder"));
}

void FOpenVolumetricAuthoringModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		OpenVolumetricEncoderTabName,
		FOnSpawnTab::CreateLambda(
			[](const FSpawnTabArgs&)
			{
				return SNew(SDockTab)
					.TabRole(ETabRole::NomadTab)
					[
						SNew(SOpenVolumetricEncoderWindow)
					];
			}))
		.SetDisplayName(NSLOCTEXT(
			"OpenVolumetricAuthoring", "EncoderTab", "OpenVolumetric Encoder"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FOpenVolumetricAuthoringModule::RegisterMenus));
}

void FOpenVolumetricAuthoringModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(
		OpenVolumetricEncoderTabName);
}

void FOpenVolumetricAuthoringModule::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(
		TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(
		TEXT("OpenVolumetric"));
	Section.AddMenuEntry(
		TEXT("OpenVolumetricEncoder"),
		NSLOCTEXT("OpenVolumetricAuthoring", "Encoder", "OpenVolumetric Encoder"),
		NSLOCTEXT(
			"OpenVolumetricAuthoring",
			"EncoderTooltip",
			"Encode image, OBJ, and audio sequences into an OpenVolumetric MP4."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda(
			[]
			{
				FGlobalTabmanager::Get()->TryInvokeTab(
					OpenVolumetricEncoderTabName);
			})));
}
