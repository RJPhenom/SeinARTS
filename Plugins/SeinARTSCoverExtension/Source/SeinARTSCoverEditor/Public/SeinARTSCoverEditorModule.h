/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverEditorModule.h
 * @brief   Editor companion to SeinARTSCover. Registers the cover provider
 *          component visualizer so designers can see slots + area volumes
 *          in the BP editor + level editor viewports.
 *
 *          Separate from SeinARTSEditor because SeinARTSCover is opt-in —
 *          forcing SeinARTSEditor to hard-depend on the cover module would
 *          break the "disable cover and the editor still works" contract.
 *          When projects disable SeinARTSCover they should disable this
 *          module too; when both are enabled they load together.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSeinARTSCoverEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
