/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatTypes.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Shared combat toolkit value types: the target query, its
 *               scored candidate, and the per-target check verdict.
 *
 *          These are mechanism types — nothing here encodes a genre opinion.
 *          The framework ships NO vitals, weapon, damage, or projectile schema:
 *          what a unit's stats are, how damage is computed, and when something
 *          dies are the consuming game's components, abilities, and effects.
 *          The query's scorer class is the one policy seam; an empty class
 *          path falls back to the neutral built-in.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "UObject/SoftObjectPath.h"
#include "SeinCombatTypes.generated.h"

/**
 * One deterministic target query. Issued on demand by abilities/effects
 * through the combat library — the framework never runs an always-on
 * engagement loop. The same struct doubles as the "can I engage THIS target
 * from here" profile for Check Target, so a designer's weapon data only needs
 * to fill one of these.
 */
USTRUCT(BlueprintType)
struct SEINARTSCOMBAT_API FSeinTargetQuery
{
	GENERATED_BODY()

	/** Entity asking (excluded from results; supplies owner + origin when
	 *  Origin is unset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FSeinEntityHandle Instigator;

	/** World-space query center. Zero = use the instigator's position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedVector Origin = FFixedVector::ZeroVector;

	/** Maximum planar range. Must be positive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint Range = FFixedPoint::Zero;

	/** Firing-arc half-angle in degrees around the instigator's facing.
	 *  180 (default) = full circle, no arc gating. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint ArcHalfAngleDegrees = FFixedPoint::FromInt(180);

	/** Candidates must carry this sim component (native or designer UDS) —
	 *  the designer's way of saying "things with MY vitals struct count as
	 *  targets". None = no component gate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	TObjectPtr<UScriptStruct> RequiredComponent = nullptr;

	/** Candidates must hold every tag listed (empty = no tag gate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FGameplayTagContainer RequiredTargetTags;

	/** Gate candidates through the fog line-of-sight resolver when bound
	 *  (unbound worlds permit, mirroring the ability LoS gate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	bool bRequireLineOfSight = true;

	/** Target scorer policy class (a USeinTargetScorer subclass). EMPTY =
	 *  the built-in neutral scorer: nearest valid candidate wins, with
	 *  same-owner entities excluded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat",
		meta = (MetaClass = "/Script/SeinARTSCombat.SeinTargetScorer"))
	FSoftClassPath ScorerClass;

	/** Bound on returned candidates (best-scored first). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat",
		meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxResults = 1;
};

/** One scored acquisition result. */
USTRUCT(BlueprintType)
struct SEINARTSCOMBAT_API FSeinTargetCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FSeinEntityHandle Target;

	/** Planar distance from the query origin. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedPoint Distance = FFixedPoint::Zero;

	/** The scorer's verdict — higher wins. Deterministic tiebreak is the
	 *  entity handle's canonical order. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedPoint Score = FFixedPoint::Zero;
};

/** Why one specific entity does or does not pass a query's gates. The gates
 *  run in this order and the first failure is reported. */
UENUM(BlueprintType)
enum class ESeinTargetCheckResult : uint8
{
	/** Passes every mechanical gate and the scorer's validity policy. */
	Eligible,
	/** Handle invalid, entity not alive, or the target is the instigator. */
	InvalidTarget,
	/** Query.RequiredComponent is set and the target does not carry it. */
	MissingComponent,
	/** Beyond Query.Range (planar). */
	OutOfRange,
	/** Outside the instigator's firing arc. */
	OutsideArc,
	/** Missing one or more Query.RequiredTargetTags. */
	MissingTags,
	/** The bound fog line-of-sight resolver denied the target's position. */
	NoLineOfSight,
	/** The scorer policy's IsValidTarget declined (built-in: same owner). */
	RejectedByScorer,
};
