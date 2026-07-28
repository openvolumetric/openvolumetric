#pragma once

#include "Modules/ModuleManager.h"

/** Runtime module containing the Unreal-facing OpenVolumetric integration. */
class FOpenVolumetricRuntimeModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
