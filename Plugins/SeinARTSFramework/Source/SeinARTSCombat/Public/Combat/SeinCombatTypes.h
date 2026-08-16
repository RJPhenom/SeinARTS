/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatTypes.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Shared combat value types: delivery kinds, damage payloads,
 *               and target query/candidate shapes.
 *
 *          These are mechanism types — nothing here encodes a genre opinion.
 *          The payload's formula class and the query's scorer class are the
 *          policy seams; empty class paths fall back to neutral built-ins.
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

/** How a weapon's fire reaches its target. Per-weapon authored data — both
 *  primitives ship in the framework; games choose per weapon. */
UENUM(BlueprintType)
enum class ESeinWeaponDelivery : uint8
{
	/** Fire resolves the same tick it is released (deterministic hit
	 *  resolution at the muzzle; tracers are presentation). The starter
	 *  weapon's default. */
	Instant,

	/** Fire spawns a pooled projectile ENTITY that flies deterministically
	 *  and resolves on impact. Projectiles are ordinary entities, so they
	 *  snapshot/replay for free and can themselves be targeted
	 *  (interception). */
	Projectile,
};

/**
 * The damage a weapon (or scripted source) delivers on a successful hit.
 * BaseDamage is an input to the formula policy, never the final number.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOMBAT_API FSeinDamagePayload
{
	GENERATED_BODY()

	/** Formula input — the authored "listed" damage before policy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint BaseDamage = FFixedPoint::FromInt(10);

	/** Damage-type tag handed to the formula (SeinARTS.Combat.Damage.*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat",
		meta = (Categories = "SeinARTS.Combat.Damage"))
	FGameplayTag DamageTypeTag;

	/** Splash radius around the impact point. Zero = single target. Every
	 *  vitals-bearing entity inside the radius receives a formula evaluation
	 *  with its own impact distance (falloff is the formula's business). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint AreaRadius = FFixedPoint::Zero;

	/** Damage formula policy class (a USeinDamageFormula subclass, resolved
	 *  by soft path so authored data never hard-loads Blueprints). EMPTY =
	 *  the built-in neutral formula: final damage equals BaseDamage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat",
		meta = (MetaClass = "/Script/SeinARTSCombat.SeinDamageFormula"))
	FSoftClassPath FormulaClass;
};

/**
 * One deterministic target query. Issued on demand by abilities/effects (and
 * the starter stance content) through the combat library — the framework
 * never runs an always-on engagement loop.
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
