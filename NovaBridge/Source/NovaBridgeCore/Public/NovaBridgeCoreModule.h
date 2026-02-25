#pragma once

#include "Modules/ModuleManager.h"

class FNovaBridgeCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
