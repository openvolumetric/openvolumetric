#include "OpenVolumetricRuntimeModule.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenVolumetric, Log, All);
IMPLEMENT_MODULE(FOpenVolumetricRuntimeModule, OpenVolumetricRuntime)

void FOpenVolumetricRuntimeModule::StartupModule()
{
	UE_LOG(LogOpenVolumetric, Log, TEXT("OpenVolumetric runtime module loaded."));
}

void FOpenVolumetricRuntimeModule::ShutdownModule()
{
	UE_LOG(LogOpenVolumetric, Log, TEXT("OpenVolumetric runtime module unloaded."));
}
