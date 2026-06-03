/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffect.h
 * @brief   Runtime state for a single active effect instance. Lightweight —
 *          all class-level config lives on the `USeinEffect` CDO referenced
 *          by `EffectClass`. Per DESIGN §8 "CDO-config, instance-runtime-state split."
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "Effects/SeinEffect.h"
#include "SeinActiveEffect.generated.h"

/**
 * Per-instance runtime state for an active effect. Lives in one of three
 * storages depending on `USeinEffect::Scope`:
 *   - Instance → entity's FSeinActiveEffectsComponent::ActiveEffects
 *   - Class    → owner's FSeinPlayerState::ClassEffects
 *   - Player → owner's FSeinPlayerState::PlayerEffects
 *
 * The class reference + CDO hold all configuration; this struct only carries
 * data that varies per application: the stack count, remaining duration,
 * periodic accumulator, source entity, and a unique instance id for targeted
 * removal.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinActiveEffect
{
	GENERATED_BODY()

	/** Unique ID assigned at apply time; used by SeinRemoveEffect for targeted removal.
	 *  Not BP-exposed directly (BP integers are int32) — the BPFL returns it as int32. */
	UPROPERTY()
	uint32 EffectInstanceID = 0;

	/** Class reference — all config reads go through GetDefault<USeinEffect>(EffectClass). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effect")
	TSubclassOf<USeinEffect> EffectClass;

	/** Remaining duration in sim-seconds. Finite effects decrement each tick;
	 *  infinite effects carry `-1` (sentinel) and never change. Instant effects
	 *  (Duration == 0) don't land in storage — they apply + remove same tick. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effect")
	FFixedPoint RemainingDuration;

	/** Accumulated time since the last periodic OnTick. Relevant only when the
	 *  CDO's TickInterval > 0. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effect")
	FFixedPoint TimeSinceLastPeriodic;

	/** Current stack count (multiplies modifier values; driven by StackingRule). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effect")
	int32 CurrentStacks = 1;

	/** Entity that applied this effect. May become stale — consumers must
	 *  validate via FSeinEntityPool::IsValid before dereferencing. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effect")
	FSeinEntityHandle Source;

	/** Entity this effect is applied to. For Class / Player scope this is the
	 *  entity whose owner drove the apply — consumers resolve to PlayerID via
	 *  USeinWorldSubsystem::GetEntityOwner. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effect")
	FSeinEntityHandle Target;
};

FORCEINLINE uint32 GetTypeHash(const FSeinActiveEffect& Effect)
{
	uint32 Hash = GetTypeHash(Effect.EffectInstanceID);
	// Hash the class by FName, NOT by pointer. CDO addresses differ between
	// processes (server's USeinEffect_FooBar* CDO has a different address
	// than each client's), so pointer-based hashing produced different
	// values on every machine for the same effect — silent state-hash
	// divergence. FName is content-based and stable across processes.
	const UClass* Cls = Effect.EffectClass.Get();
	Hash = HashCombine(Hash, Cls ? GetTypeHash(Cls->GetFName()) : 0u);
	Hash = HashCombine(Hash, GetTypeHash(Effect.RemainingDuration));
	// TimeSinceLastPeriodic is sim-state-relevant (drives periodic OnTick
	// firing) — must be hashed or periodic-effect drift goes undetected.
	Hash = HashCombine(Hash, GetTypeHash(Effect.TimeSinceLastPeriodic));
	Hash = HashCombine(Hash, GetTypeHash(Effect.CurrentStacks));
	Hash = HashCombine(Hash, GetTypeHash(Effect.Source));
	Hash = HashCombine(Hash, GetTypeHash(Effect.Target));
	return Hash;
}
