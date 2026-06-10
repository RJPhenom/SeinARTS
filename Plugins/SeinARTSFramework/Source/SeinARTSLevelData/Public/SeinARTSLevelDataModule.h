#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSLevelDataModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
