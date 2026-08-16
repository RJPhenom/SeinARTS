/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinProjectileComponent.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Deterministic in-flight projectile state.
 *
 *          A projectile is an ordinary pooled ENTITY carrying this component
 *          — canonical state, snapshot, and replay come free, and because it
 *          is a real entity it can itself carry vitals and be shot down
 *          (interception). Flight is driven by the projectile system: home on
 *          the target entity while it lives, otherwise fly to the last known
 *          point; resolve the payload on arrival. Sim-only when spawned
 *          abstract; author a projectile unit Blueprint on the weapon slot
 *          for rendered shells/tracers.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"
#include "Components/SeinComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinProjectileComponent.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, DisplayName = "Projectile Component"))
struct SEINARTSCOMBAT_API FSeinProjectileComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Entity credited with the eventual damage. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FSeinEntityHandle Instigator;

	/** Homing target. While alive, flight chases its current position and
	 *  refreshes LastKnownTargetPoint each tick. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FSeinEntityHandle Target;

	/** Where the projectile is headed when the target entity is gone. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedVector LastKnownTargetPoint = FFixedVector::ZeroVector;

	/** Flight speed (world units / second). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedPoint Speed = FFixedPoint::FromInt(2000);

	/** Delivered on impact. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FSeinDamagePayload Payload;

	/** Fail-safe: a projectile that never arrives self-destructs (no impact)
	 *  when this reaches zero. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedPoint LifetimeRemaining = FFixedPoint::FromInt(10);
};

FORCEINLINE uint32 GetTypeHash(const FSeinProjectileComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.Instigator);
	Hash = HashCombine(Hash, GetTypeHash(Component.Target));
	// The per-tick-refreshed aim point is mutable flight state — omitting it
	// would blind the component's own hash to a homing divergence until it
	// leaked into the transform a tick later.
	Hash = HashCombine(Hash, GetTypeHash(Component.LastKnownTargetPoint));
	Hash = HashCombine(Hash, GetTypeHash(Component.Speed));
	Hash = HashCombine(Hash, GetTypeHash(Component.LifetimeRemaining));
	return Hash;
}
