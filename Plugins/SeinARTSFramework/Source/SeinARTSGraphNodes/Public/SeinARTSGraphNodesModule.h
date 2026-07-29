/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSGraphNodesModule.h
 * @brief   Module shell for the SeinARTS GraphNodes module — hosts K2 nodes
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
 *            FSeinComponent substruct + every UDS the designer authored via
 *            Right-click → Sein Component, exposing them as separate action-
 *            menu entries with pre-typed Break-Struct-compatible pins.
 *
 *          Future K2 nodes for other subsystems (movement, cover, FoW) land
 *          here too — one shared graph-nodes module per plugin keeps the dep
 *          graph simple.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FSeinARTSGraphNodesModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;

private:
	/**
	 * Synchronously sever editor-owned references to this generation's node
	 * delegates and template nodes. PreUnloadCallback and ShutdownModule share
	 * this idempotent path so dynamic module reload cannot leave executable
	 * callbacks into an unloaded DLL.
	 */
	void ReleaseModuleOwnedState();

	bool bModuleOwnedStateReleased = false;
};
