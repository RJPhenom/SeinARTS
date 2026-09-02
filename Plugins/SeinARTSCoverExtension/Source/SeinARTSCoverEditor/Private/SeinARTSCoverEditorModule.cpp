/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverEditorModule.cpp
 *
 * Cover editor module — owns two editor-surface integrations for the cover
 * system:
 *
 *   1. Property-type customization (`FSeinCoverComponentDetails`) for
 *      FSeinCoverPayload, injecting the "Generate Slots" button into the
 *      details panel for any cover component authored on the entity
 *      bridge's ComponentData array.
 *
 *   2. Entity-bridge draw callback (`SeinCoverEntityDraw::DrawCoverEntries`)
 *      registered with `FSeinARTSEditorModule::RegisterComponentDataDraw`
 *      so the bridge's single visualizer fans out to our cover-area + slot
 *      draw layer. SeinARTSEditor doesn't know about us — clean opt-in.
 */

#include "SeinARTSCoverEditorModule.h"
#include "Details/SeinCoverComponentDetails.h"
#include "Visualizers/SeinCoverEntityDraw.h"

#include "SeinARTSEditorModule.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSCoverEditor, Log, All);

namespace
{
	// Registry key for the cover draw callback. Stored as a constant so the
	// register + unregister sites can't drift on a typo'd name.
	const FName GCoverDrawKey(TEXT("SeinCoverComponent"));
}

void FSeinARTSCoverEditorModule::StartupModule()
{
	UE_LOG(LogSeinARTSCoverEditor, Log, TEXT("SeinARTSCoverEditor module started."));

	// Property-type customization for FSeinCoverPayload. Registers by struct
	// short-name (without the F prefix) — UE's property-editor module matches
	// against `UScriptStruct::GetFName()`, which is the bare struct name.
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(
		TEXT("SeinCoverComponent"),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FSeinCoverComponentDetails::MakeInstance));
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
		Draw.BindStatic(&SeinCoverEntityDraw::DrawCoverEntries);
		EditorModule->RegisterComponentDataDraw(GCoverDrawKey, Draw);
		UE_LOG(LogSeinARTSCoverEditor, Log,
			TEXT("Registered cover-entity draw callback (key=%s). Registry now has %d entries."),
			*GCoverDrawKey.ToString(),
			EditorModule->GetComponentDataDraws().Num());
	}
	else
	{
		UE_LOG(LogSeinARTSCoverEditor, Warning,
			TEXT("Could not resolve FSeinARTSEditorModule — cover entity viz will NOT fire. "
			     "Check load phase / module dep / type cast."));
	}
}

void FSeinARTSCoverEditorModule::ShutdownModule()
{
	UE_LOG(LogSeinARTSCoverEditor, Log, TEXT("SeinARTSCoverEditor module shut down."));

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout(TEXT("SeinCoverComponent"));
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	// Drop our entity-bridge draw callback. GetModulePtr (not Checked) so a
	// teardown ordering issue — where SeinARTSEditor unloaded first — doesn't
	// assert. The editor module's own ShutdownModule clears the registry as
	// a safety net anyway.
	if (FSeinARTSEditorModule* EditorModule =
			FModuleManager::GetModulePtr<FSeinARTSEditorModule>("SeinARTSEditor"))
	{
		EditorModule->UnregisterComponentDataDraw(GCoverDrawKey);
	}
}

IMPLEMENT_MODULE(FSeinARTSCoverEditorModule, SeinARTSCoverEditor)
