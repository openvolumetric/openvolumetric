#include "OpenVolumetricRuntimeModule.h"

#include "Modules/ModuleManager.h"
#include "Logger.h"

DEFINE_LOG_CATEGORY_STATIC(LogOpenVolumetric, Log, All);
IMPLEMENT_MODULE(FOpenVolumetricRuntimeModule, OpenVolumetricRuntime)

namespace
{
void ForwardNativeLog(
	openvolumetric::LogLevel Level,
	const char* Message,
	void*)
{
	const FString Text = UTF8_TO_TCHAR(Message != nullptr ? Message : "");
	switch (Level)
	{
	case openvolumetric::LogLevel::Error:
		UE_LOG(LogOpenVolumetric, Error, TEXT("%s"), *Text);
		break;
	case openvolumetric::LogLevel::Warning:
		UE_LOG(LogOpenVolumetric, Warning, TEXT("%s"), *Text);
		break;
	case openvolumetric::LogLevel::Debug:
		UE_LOG(LogOpenVolumetric, Verbose, TEXT("%s"), *Text);
		break;
	case openvolumetric::LogLevel::Info:
	default:
		UE_LOG(LogOpenVolumetric, Log, TEXT("%s"), *Text);
		break;
	}
}
}

void FOpenVolumetricRuntimeModule::StartupModule()
{
	openvolumetric::Logger::instance().set_callback(ForwardNativeLog, nullptr);
	UE_LOG(LogOpenVolumetric, Log, TEXT("OpenVolumetric runtime module loaded."));
}

void FOpenVolumetricRuntimeModule::ShutdownModule()
{
	UE_LOG(LogOpenVolumetric, Log, TEXT("OpenVolumetric runtime module unloaded."));
	openvolumetric::Logger::instance().clear_callback(ForwardNativeLog, nullptr);
}
