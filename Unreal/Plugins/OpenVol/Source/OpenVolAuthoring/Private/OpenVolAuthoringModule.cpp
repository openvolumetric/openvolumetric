#include "OpenVolAuthoringModule.h"

#include "OpenVolEncoderWindow.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"

IMPLEMENT_MODULE(FOpenVolAuthoringModule, OpenVolAuthoring)

namespace
{
const FName OpenVolEncoderTabName(TEXT("OpenVolEncoder"));
}

void FOpenVolAuthoringModule::StartupModule()
{
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		OpenVolEncoderTabName,
		FOnSpawnTab::CreateLambda(
			[](const FSpawnTabArgs&)
			{
				return SNew(SDockTab)
					.TabRole(ETabRole::NomadTab)
					[
						SNew(SOpenVolEncoderWindow)
					];
			}))
		.SetDisplayName(NSLOCTEXT(
			"OpenVolAuthoring", "EncoderTab", "OpenVol Encoder"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FOpenVolAuthoringModule::RegisterMenus));
}

void FOpenVolAuthoringModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(
		OpenVolEncoderTabName);
}

void FOpenVolAuthoringModule::RegisterMenus()
{
	FToolMenuOwnerScoped Owner(this);
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(
		TEXT("LevelEditor.MainMenu.Tools"));
	FToolMenuSection& Section = Menu->FindOrAddSection(
		TEXT("OpenVol"));
	Section.AddMenuEntry(
		TEXT("OpenVolEncoder"),
		NSLOCTEXT("OpenVolAuthoring", "Encoder", "OpenVol Encoder"),
		NSLOCTEXT(
			"OpenVolAuthoring",
			"EncoderTooltip",
			"Encode image, OBJ, and audio sequences into an OpenVol MP4."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda(
			[]
			{
				FGlobalTabmanager::Get()->TryInvokeTab(
					OpenVolEncoderTabName);
			})));
}
