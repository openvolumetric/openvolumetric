#pragma once

#include "Modules/ModuleManager.h"

/** Runtime module containing the Unreal-facing OpenVol integration. */
class FOpenVolRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
