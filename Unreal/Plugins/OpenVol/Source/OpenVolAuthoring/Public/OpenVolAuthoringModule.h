#pragma once

#include "Modules/ModuleManager.h"

/** Editor-only module that exposes OpenVol container authoring in Unreal. */
class FOpenVolAuthoringModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterMenus();
};
