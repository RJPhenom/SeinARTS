/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinConstructionBPFL.h
 * @brief   Blueprint helpers for Pattern-B construction-over-time. Designers
 *          drop these into their BA_Construct ability's OnTick to advance
 *          progress on a target, and into their HUD to bind progress bars.
 *
 *          See SeinConstructionComponent.h for the full lifecycle (placement →
 *          builder ticks → completion-effect transition). This BPFL is the
 *          BP-facing surface; sim-side code is in SeinConstructionBPFL.cpp.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "SeinConstructionBPFL.generated.h"

UCLASS()
class SEINARTSCOREENTITY_API USeinConstructionBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True if the entity has an active FSeinConstructionComponent (Progress < BuildTime).
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
	 *  construction component, the increment didn't cross the threshold, or
	 *  the entity is already finished.
	 *
	 *  On auto-finish: applies CompletionEffect (if set on the data) to the
	 *  entity, removes the FSeinConstructionComponent component, ungrants the
	 *  SeinARTS.State.UnderConstruction tag.
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
	 *  (if set), removes the FSeinConstructionComponent component, ungrants the
	 *  SeinARTS.State.UnderConstruction tag. Use for cheats / debug, or for
	 *  ability designs that want a "snap-finish" path independent of the
	 *  per-tick progression (e.g. instant-build cheat ability). No-op if the
	 *  entity has no active construction component. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Construction",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Finish Construction"))
	static void SeinFinishConstruction(const UObject* WorldContextObject, FSeinEntityHandle Entity);
};
