/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSFogOfWarEditorModule.h
 * @brief   Editor companion to SeinARTSFogOfWar. Registers the vision-stamp
 *          draw layer on the entity bridge visualizer. (Fog baking runs through
 *          the unified "Bake Level Data" button on ASeinLevelVolume.)
 *
 *          Separate from the SeinARTSFogOfWar Runtime module because the FoW
 *          system needs to load at Default phase for sim availability — but
 *          its editor-side pieces need SeinARTSEditor (PostEngineInit phase)
 *          to already be loaded. Splitting the editor pieces into a dedicated
 *          Editor / PostEngineInit module mirrors the SeinARTSCoverEditor
 *          pattern and eliminates the load-order race that breaks the
 *          OnFEngineLoopInitComplete deferred registration path.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSeinARTSFogOfWarEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
