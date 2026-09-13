/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinComponentNodeMenuCache.h
 * @brief   Shared candidate-struct enumeration for the typed component K2 nodes
 *          (UK2Node_SeinGetComponent / UK2Node_SeinSetComponent).
 *
 *          Both nodes build their action-menu entries from the same set: every
 *          loaded native `FSeinPayload` substruct plus every eligible entity
 *          component UDS. Each node's `GetMenuActions` is invoked once
 *          per Blueprint action-database rebuild, and the two nodes are visited
 *          back-to-back within the SAME rebuild — so an unshared scan walks every
 *          loaded `UScriptStruct` twice per rebuild.
 *
 *          This helper memoizes the scan for the duration of one rebuild, keyed
 *          on the engine frame counter: the first node to ask does the scan, the
 *          second reuses it. GraphNodes loads saved component assets outside
 *          action registration and resets this cache before explicit Get/Set
 *          refreshes, including refreshes in the same frame as an earlier scan.
 *
 *          Note on coupling: the two nodes previously inlined this scan
 *          separately "to stay loosely coupled." They share it now purely to
 *          de-duplicate identical work; if a node ever needs a different
 *          candidate predicate, it can stop calling this and inline its own
 *          again — the shared helper imposes no policy beyond today's behavior.
 */

#pragma once

#include "CoreMinimal.h"

class UScriptStruct;

namespace SeinComponentNodeMenu
{
	/** Fill `Out` with the FSeinPayload-eligible candidate structs for the BP
	 *  action menu (native FSeinPayload children + eligible entity component UDS),
	 *  sorted by name. Memoized per action-DB rebuild (see file header). */
	void GetCandidateStructs(TArray<UScriptStruct*>& Out);

	/** Invalidate candidates before a component action refresh or DLL unload. */
	void ResetCandidateCache();
}
