#include "OpenVolRuntimeModule.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenVol, Log, All);
IMPLEMENT_MODULE(FOpenVolRuntimeModule, OpenVolRuntime)

void FOpenVolRuntimeModule::StartupModule()
{
	UE_LOG(LogOpenVol, Log, TEXT("OpenVol runtime module loaded."));
}

void FOpenVolRuntimeModule::ShutdownModule()
{
	UE_LOG(LogOpenVol, Log, TEXT("OpenVol runtime module unloaded."));
}
