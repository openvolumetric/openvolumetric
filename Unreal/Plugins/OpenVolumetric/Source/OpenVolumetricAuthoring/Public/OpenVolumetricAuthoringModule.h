#pragma once

#include "Modules/ModuleManager.h"

/** Editor-only module that exposes OpenVolumetric container authoring in Unreal. */
class FOpenVolumetricAuthoringModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
};
