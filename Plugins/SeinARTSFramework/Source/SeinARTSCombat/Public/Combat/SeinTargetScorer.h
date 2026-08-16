/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinTargetScorer.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Target validity + scoring policy seam for acquisition.
 *
 *          The query service owns the deterministic sweep (range, arc, LoS,
 *          tag gates, canonical ordering); this class owns WHICH candidates
 *          count and WHO wins — the policy half. Stateless CDO evaluation,
 *          Blueprint-subclassable. The neutral built-in excludes same-owner
 *          entities and scores by proximity; games layer threat, alliances
 *          (pair-capability checks), veterancy focus, or intercept priority
 *          by overriding two functions.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "UObject/Object.h"
#include "SeinTargetScorer.generated.h"

class USeinWorldSubsystem;

UCLASS(Blueprintable, meta = (DisplayName = "Target Scorer"))
class SEINARTSCOMBAT_API USeinTargetScorer : public UObject
{
	GENERATED_BODY()

public:
	/** May this candidate be targeted at all? Runs after the mechanical
	 *  gates (alive, vitals present, range, arc, tags, LoS). The default
	 *  excludes the instigator's own entities — hostility is policy, so
	 *  alliance-aware games override this (e.g. consult pair capabilities). */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "SeinARTS|Combat")
	bool IsValidTarget(
		const USeinWorldSubsystem* World,
		const FSeinTargetQuery& Query,
		FSeinEntityHandle Candidate) const;
	virtual bool IsValidTarget_Implementation(
		const USeinWorldSubsystem* World,
		const FSeinTargetQuery& Query,
		FSeinEntityHandle Candidate) const;

	/** Score a valid candidate — highest wins; exact ties break on canonical
	 *  entity order. The default scores nearer as higher. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "SeinARTS|Combat")
	FFixedPoint ScoreTarget(
		const USeinWorldSubsystem* World,
		const FSeinTargetQuery& Query,
		const FSeinTargetCandidate& Candidate) const;
	virtual FFixedPoint ScoreTarget_Implementation(
		const USeinWorldSubsystem* World,
		const FSeinTargetQuery& Query,
		const FSeinTargetCandidate& Candidate) const;
};
