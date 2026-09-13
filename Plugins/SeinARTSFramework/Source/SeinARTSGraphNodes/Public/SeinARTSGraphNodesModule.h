/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSGraphNodesModule.h
 * @author  RJ Macklem
 * @created 7 Sep 2026
 * @latest  7 Sep 2026
 * @brief   Owns component action discovery and refresh, and hosts K2 nodes
 *          (custom Blueprint graph nodes) for the framework's BP authoring
 *          tooling. The module is `Type=UncookedOnly` so the K2 nodes load
 *          in the editor + during cook commandlets (where `ExpandNode`
 *          rewrites them into regular function calls in the BP bytecode)
 *          but are stripped from shipping builds where they have no purpose.
 *
 *          Why a dedicated module:
 *          - K2 nodes can't live in Runtime modules (they'd ship as dead
 *            code; UE doesn't allow Runtime modules to depend on the
 *            editor-only KismetCompiler / BlueprintGraph deps K2 nodes need).
 *          - K2 nodes can't live in Editor-typed modules either — UE emits
 *            a warning when a K2 node from an Editor module is placed in a
 *            runtime BP: "K2 Nodes should only be defined in a Developer or
 *            UncookedOnly module."
 *
 *          What lives here:
 *          - `UK2Node_SeinGetComponent` / `UK2Node_SeinSetComponent` —
 *            typed-pin Get/Set nodes that wrap `USeinComponentBPFL`'s
 *            wildcard component accessors. Auto-discovers every native
 *            FSeinPayload substruct + every UDS the designer authored via
 *            Right-click → Sein Component, exposing them as separate action-
 *            menu entries with pre-typed Break-Struct-compatible pins.
 *
 *          Future K2 nodes for other subsystems (movement, cover, FoW) land
 *          here too — one shared graph-nodes module per plugin keeps the dep
 *          graph simple.
 *
 * @disclaimer Generated with assistance from an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleManager.h"

class SEINARTSGRAPHNODES_API FSeinARTSGraphNodesModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;

	/** Queue a Get/Set action refresh after component payload generation finishes.
	 *  Discovery also loads saved component types that have not been opened. */
	void RequestComponentActionsRefresh(bool bDiscoverAssets = false);

private:
	bool TickComponentActions(float DeltaTime);
	void RegisterAbilityCompileHook();
	void OnAbilityBlueprintCompiled();
	FDelegateHandle AbilityEngineInitHandle;
	bool TickAbilityInputs(float DeltaTime);
	FDelegateHandle AbilityCompiledHandle;
	FTSTicker::FDelegateHandle AbilityInputsTicker;
	void OnComponentAssetChanged(const struct FAssetData& Asset);
	void OnComponentAssetRenamed(const struct FAssetData& Asset, const FString& OldPath);
	void OnComponentObjectLoaded(UObject* Object);
	void OnComponentFilesLoaded();
	void OnComponentsPreDelete(const TArray<UObject*>& Objects);

	FTSTicker::FDelegateHandle ComponentActionsTicker;
	FDelegateHandle ComponentAssetAdded;
	FDelegateHandle ComponentAssetRemoved;
	FDelegateHandle ComponentAssetUpdated;
	FDelegateHandle ComponentAssetRenamed;
	FDelegateHandle ComponentObjectLoaded;
	FDelegateHandle ComponentFilesLoaded;
	FDelegateHandle ComponentPreDelete;
	FDelegateHandle ComponentPreForceDelete;
	bool bDiscoverComponentAssets = false;
	bool bRefreshingComponentActions = false;

	/**
	 * Synchronously sever editor-owned references to this generation's node
	 * delegates and template nodes. PreUnloadCallback and ShutdownModule share
	 * this idempotent path so dynamic module reload cannot leave executable
	 * callbacks into an unloaded DLL.
	 */
	void ReleaseModuleOwnedState();

	bool bModuleOwnedStateReleased = false;
};
