/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNet.h
 * @brief   Module entry point + log category for the lockstep network layer.
 *
 * Owns the lockstep transport, lobby, replay, compatibility, and session
 * lifecycle module.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSeinNet, Log, All);

class FSeinARTSNetModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;
};
