/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatBPFL.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Read-side combat toolkit queries for any Blueprint graph:
 *               acquisition and per-target eligibility.
 *
 *          The framework owns the deterministic WHO-IS-IN-REACH mechanics
 *          (range, arc, tags, fog LoS, component presence, scorer dispatch,
 *          canonical ordering). It owns nothing about what a hit does — the
 *          designer's vitals struct, damage math, and death rule live in their
 *          own components, abilities, and effects, mutated through the generic
 *          `Apply Field Delta` / `Apply Effect` / `Destroy Entity` nodes.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SeinCombatBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Combat Library"))
class SEINARTSCOMBAT_API USeinCombatBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Run one deterministic acquisition query (see FSeinTargetQuery):
	 *  best-scored candidates first, canonical tiebreak, bounded by
	 *  Query.MaxResults. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Find Targets"))
	static TArray<FSeinTargetCandidate> SeinFindTargets(
		const UObject* WorldContextObject, const FSeinTargetQuery& Query);

	/** Evaluate ONE specific entity against the query's gates — the same
	 *  chain Find Targets applies — and report the first failing gate (or
	 *  Eligible). Use it for "may I engage the thing the player clicked from
	 *  here" and for UI feedback (out of range / no line of sight). When
	 *  Eligible, Candidate carries the distance and score. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Check Target"))
	static ESeinTargetCheckResult SeinCheckTarget(
		const UObject* WorldContextObject,
		const FSeinTargetQuery& Query,
		FSeinEntityHandle Target,
		FSeinTargetCandidate& Candidate);
};
