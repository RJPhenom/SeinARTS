/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSGraphNodesModule.cpp
 */

#include "SeinARTSGraphNodesModule.h"

#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "Framework/Application/SlateApplication.h"
#include "Graph/K2Node_SeinGetComponent.h"
#include "Graph/K2Node_SeinSetComponent.h"
#include "Graph/SeinComponentNodeMenuCache.h"
#include "Widgets/SeinWidgetBlueprint.h"
#include "WidgetBlueprint.h"
#include "KismetCompiler.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	void ReleaseNodeClassActions(
		FBlueprintActionDatabase& ActionDatabase,
		UClass* NodeClass)
	{
		const FBlueprintActionDatabase::FActionRegistry& Registry =
			ActionDatabase.GetAllActions();
		const FBlueprintActionDatabase::FActionList* Actions =
			Registry.Find(FObjectKey(NodeClass));

		if (Actions != nullptr)
		{
			for (UBlueprintNodeSpawner* Spawner : *Actions)
			{
				if (Spawner == nullptr)
				{
					continue;
				}

				// Clear executable module callbacks before dropping the action
				// database's GC reference to the spawner. A menu may have
				// primed and cached a live template node already.
				Spawner->CustomizeNodeDelegate.Unbind();
				Spawner->ClearCachedTemplateNode();
			}
		}

		ActionDatabase.ClearAssetActions(NodeClass);
	}
}

void FSeinARTSGraphNodesModule::StartupModule()
{
	bModuleOwnedStateReleased = false;

	// UMG's compiler registry is keyed by the exact Blueprint asset class and
	// does not walk its inheritance chain. Register from this UncookedOnly
	// module (rather than the Editor-typed tooling module) so editor, cook
	// commandlets, and UnrealEditor -game all compile/load Sein widget assets
	// with their WidgetTree intact.
	FKismetCompilerContext::RegisterCompilerForBP(
		USeinWidgetBlueprint::StaticClass(),
		&UWidgetBlueprint::GetCompilerForWidgetBP);
}

void FSeinARTSGraphNodesModule::PreUnloadCallback()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSGraphNodesModule::ShutdownModule()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSGraphNodesModule::ReleaseModuleOwnedState()
{
	if (bModuleOwnedStateReleased)
	{
		return;
	}
	bModuleOwnedStateReleased = true;

	// Open Blueprint action menus may retain their own references to the
	// spawners. Close them while this generation's callbacks are still valid.
	if (!IsEngineExitRequested() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().DismissAllMenus();
	}

	if (UObjectInitialized() && !IsEngineExitRequested())
	{
		if (FBlueprintActionDatabase* ActionDatabase =
				FBlueprintActionDatabase::TryGet())
		{
			ReleaseNodeClassActions(
				*ActionDatabase,
				UK2Node_SeinGetComponent::StaticClass());
			ReleaseNodeClassActions(
				*ActionDatabase,
				UK2Node_SeinSetComponent::StaticClass());
		}
	}

	SeinComponentNodeMenu::ResetCandidateCache();
}

IMPLEMENT_MODULE(FSeinARTSGraphNodesModule, SeinARTSGraphNodes)
