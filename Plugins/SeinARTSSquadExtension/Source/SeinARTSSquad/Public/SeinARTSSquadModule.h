/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 */

#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSSquadModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
