/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNet.h
 * @brief   Module entry point + log category for the lockstep network layer.
 *
 * Phase 0 scope: relay actor + game-instance subsystem proving an FSeinCommand
 * round-trips through Unreal RPCs (host -> client + client -> host -> all)
 * in a PIE Listen Server session. No sim integration yet — that's Phase 2.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSeinNet, Log, All);

class FSeinARTSNetModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
