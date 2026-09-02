/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinConstructionBPFL.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       21 Aug 2026
 * @brief        Exposes deterministic construction progress and completion operations to Blueprint.
 *
 *               See SeinConstructionComponent.h for the placement, builder-tick,
 *               and completion-effect lifecycle.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "SeinConstructionBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Construction Library", SeinDeterministic))
class SEINARTSCOREENTITY_API USeinConstructionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True if the entity has an active FSeinConstructionPayload (Progress < BuildTime).
	 *  Use to gate ability activation server-side, drive UI ("show progress bar"),
	 *  or filter selection. Returns false if the entity has no construction
	 *  component or has already been finished. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Construction",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Under Construction"))
	static bool SeinIsUnderConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Returns Progress / BuildTime as a 0..1 ratio. Useful for progress-bar
	 *  bindings. Returns 0 when the entity has no construction component;
	 *  returns 1 when complete (briefly, before the component is removed). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Construction",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Construction Percent"))
	static FFixedPoint SeinGetConstructionPercent(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Add to the entity's construction Progress and auto-finish if the
	 *  threshold is crossed. Returns true if construction completed THIS call
	 *  (so the caller's BA_Construct can EndAbility cleanly without needing
	 *  to poll IsUnderConstruction). Returns false if the entity has no
	 *  construction component, Amount is non-positive or would overflow, the
	 *  increment didn't cross the threshold, or the entity is already finished.
	 *  A non-positive authored completion time finishes on the first valid call.
	 *
	 *  On auto-finish: applies CompletionEffect (if set on the data) to the
	 *  entity, removes the FSeinConstructionPayload component, and releases
	 *  the framework-owned SeinARTS.State.UnderConstruction tag grant. An
	 *  independent designer-authored BaseTags grant is preserved.
	 *
	 *  Multiple builders ticking the same target stack additively — each
	 *  call adds Amount to Progress regardless of who's calling. Designer
	 *  applies per-worker speed multipliers in the BA_Construct ability if
	 *  diminishing-returns scaling is desired. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Add Construction Progress"))
	static bool SeinAddConstructionProgress(const UObject* WorldContextObject, FSeinEntityHandle Entity,
		FFixedPoint Amount);

	/** Force-complete the construction immediately. Applies CompletionEffect
	 *  (if set), removes the FSeinConstructionPayload component, and releases
	 *  the framework-owned SeinARTS.State.UnderConstruction tag grant. Use for
	 *  cheats / debug, or for ability designs that want a "snap-finish" path independent of the
	 *  per-tick progression (e.g. instant-build cheat ability). No-op if the
	 *  entity has no active construction component. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Finish Construction"))
	static void SeinFinishConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity);
};
