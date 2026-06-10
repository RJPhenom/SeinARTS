/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSFogOfWarEditorModule.cpp
 *
 * Owns the editor-side integration for the FoW system: the entity-bridge
 * draw callback (`SeinFogOfWarEntityDraw::DrawVisionStamps`) registered with
 * `FSeinARTSEditorModule::RegisterComponentDataDraw` so the bridge's single
 * visualizer fans out to our vision-stamp draw layer.
 *
 * (The legacy ASeinFogOfWarVolume details panel / "Bake Fog Of War" button
 * was retired with the unified level-data pipeline — baking now runs through
 * the "Bake Level Data" button on ASeinLevelVolume.)
 *
 * Lives in its own Editor / PostEngineInit module so that by the time
 * StartupModule fires, SeinARTSEditor is already loaded — no load-order race,
 * no deferred delegates. Mirrors the SeinARTSCoverEditor pattern.
 */

#include "SeinARTSFogOfWarEditorModule.h"
#include "Visualizers/SeinFogOfWarEntityDraw.h"

#include "SeinARTSEditorModule.h"

#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSFogOfWarEditor, Log, All);

namespace
{
	// Registry key for the vision-stamp draw callback. Stored as a constant
	// so the register + unregister sites can't drift on a typo'd name.
	const FName GVisionStampDrawKey(TEXT("SeinVisionComponent"));
}

void FSeinARTSFogOfWarEditorModule::StartupModule()
{
	UE_LOG(LogSeinARTSFogOfWarEditor, Log, TEXT("SeinARTSFogOfWarEditor module started."));

	// Entity-bridge draw callback. LoadModulePtr ensures SeinARTSEditor is
	// loaded before we hand it a delegate — Build.cs deps already enforce
	// this, but the explicit load guards against startup-order weirdness on
	// hot-reload paths.
	FSeinARTSEditorModule* EditorModule =
		FModuleManager::LoadModulePtr<FSeinARTSEditorModule>("SeinARTSEditor");
	if (EditorModule)
	{
		FSeinComponentDataDrawDelegate Draw;
		Draw.BindStatic(&SeinFogOfWarEntityDraw::DrawVisionStamps);
		EditorModule->RegisterComponentDataDraw(GVisionStampDrawKey, Draw);
		UE_LOG(LogSeinARTSFogOfWarEditor, Log,
			TEXT("Registered vision-stamp draw callback (key=%s). Registry now has %d entries."),
			*GVisionStampDrawKey.ToString(),
			EditorModule->GetComponentDataDraws().Num());
	}
	else
	{
		UE_LOG(LogSeinARTSFogOfWarEditor, Warning,
			TEXT("Could not resolve FSeinARTSEditorModule — vision-stamp viz will NOT fire. "
			     "Check load phase / module dep / type cast."));
	}
}

void FSeinARTSFogOfWarEditorModule::ShutdownModule()
{
	UE_LOG(LogSeinARTSFogOfWarEditor, Log, TEXT("SeinARTSFogOfWarEditor module shut down."));

	// Drop our entity-bridge draw callback. GetModulePtr (not Checked) so a
	// teardown ordering issue — where SeinARTSEditor unloaded first — doesn't
	// assert. The editor module's own ShutdownModule clears the registry as
	// a safety net anyway.
	if (FSeinARTSEditorModule* EditorModule =
			FModuleManager::GetModulePtr<FSeinARTSEditorModule>("SeinARTSEditor"))
	{
		EditorModule->UnregisterComponentDataDraw(GVisionStampDrawKey);
	}
}

IMPLEMENT_MODULE(FSeinARTSFogOfWarEditorModule, SeinARTSFogOfWarEditor)
