/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSFogOfWarEditorModule.cpp
 *
 * Owns the editor-side integrations for the FoW system:
 *
 *   1. Volume details panel (`FSeinFogOfWarVolumeDetails`) — adds the
 *      "Bake Fog Of War" button to ASeinFogOfWarVolume.
 *
 *   2. Entity-bridge draw callback (`SeinFogOfWarEntityDraw::DrawVisionStamps`)
 *      registered with `FSeinARTSEditorModule::RegisterComponentDataDraw` so
 *      the bridge's single visualizer fans out to our vision-stamp draw layer.
 *
 * Lives in its own Editor / PostEngineInit module so that by the time
 * StartupModule fires, SeinARTSEditor + PropertyEditor are both already
 * loaded — no load-order race, no deferred delegates. Mirrors the
 * SeinARTSCoverEditor pattern.
 */

#include "SeinARTSFogOfWarEditorModule.h"
#include "Details/SeinFogOfWarVolumeDetails.h"
#include "Visualizers/SeinFogOfWarEntityDraw.h"

#include "SeinARTSEditorModule.h"
#include "Volumes/SeinFogOfWarVolume.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

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

	// Volume details panel — "Bake Fog Of War" button on ASeinFogOfWarVolume.
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout(
		ASeinFogOfWarVolume::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FSeinFogOfWarVolumeDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();

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

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ASeinFogOfWarVolume::StaticClass()->GetFName());
	}

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
