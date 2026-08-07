/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffect.h
 * @brief   Runtime state for a single active effect instance. Lightweight —
 *          all class-level config lives on the `USeinEffect` CDO referenced
 *          by `EffectClass`. Per DESIGN §8 "CDO-config, instance-runtime-state split."
 */

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Abilities/SeinAbility.h"
#include "Types/FixedPoint.h"
#include "Core/SeinEntityHandle.h"
#include "Effects/SeinEffect.h"
#include "SeinActiveEffect.generated.h"

/** One ability reference actually committed by an active effect. Stored on the
 *  effect so teardown revokes exactly what that instance granted, even when a
 *  passive callback removes it partway through its authored grant list. */
USTRUCT(meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinEffectAbilityGrant
{
	GENERATED_BODY()

	UPROPERTY()
	FSeinEntityHandle Recipient;

	UPROPERTY()
	TSubclassOf<USeinAbility> AbilityClass;
};

FORCEINLINE uint32 GetTypeHash(const FSeinEffectAbilityGrant& Grant)
{
	uint32 Hash = GetTypeHash(Grant.Recipient);
	const UClass* AbilityClass = Grant.AbilityClass.Get();
	return HashCombine(Hash, AbilityClass ? GetTypeHash(AbilityClass->GetFName()) : 0u);
}

template<>
struct TStructOpsTypeTraits<FSeinEffectAbilityGrant>
	: public TStructOpsTypeTraitsBase2<FSeinEffectAbilityGrant>
{
	enum { WithGetTypeHash = true };
};

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

	/** World-global deterministic ID assigned when this instance is committed.
	 *  Zero is invalid; IDs are never reused within a simulation timeline. */
	UPROPERTY()
	int64 EffectInstanceID = 0;

	/** Class reference — all config reads go through GetDefault<USeinEffect>(EffectClass). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Effect")
	TSubclassOf<USeinEffect> EffectClass;

	/** Remaining duration in sim-seconds. Timed effects decrement each tick;
	 *  Persistent effects ignore this field. Instant effects land only long
	 *  enough to dispatch apply/removal lifecycle hooks in the same call. */
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

	/** Deterministic ownership ledger for successful GrantedAbilities calls.
	 *  An entry is reserved before passive activation can run, then removed if
	 *  the grant does not commit. Snapshotting this array keeps later teardown
	 *  balanced after save/load and lockstep catch-up. */
	UPROPERTY()
	TArray<FSeinEffectAbilityGrant> CommittedAbilityGrants;
};

FORCEINLINE uint32 GetTypeHash(const FSeinActiveEffect& Effect)
{
	uint32 Hash = GetTypeHash(Effect.EffectInstanceID);
	// Avoid process-local CDO pointer bits. FName is the current class-key
	// representation; fully canonical cross-process class hashing remains
	// tracked separately under STATE-02.
	const UClass* Cls = Effect.EffectClass.Get();
	Hash = HashCombine(Hash, Cls ? GetTypeHash(Cls->GetFName()) : 0u);
	Hash = HashCombine(Hash, GetTypeHash(Effect.RemainingDuration));
	// TimeSinceLastPeriodic is sim-state-relevant (drives periodic OnTick
	// firing) — must be hashed or periodic-effect drift goes undetected.
	Hash = HashCombine(Hash, GetTypeHash(Effect.TimeSinceLastPeriodic));
	Hash = HashCombine(Hash, GetTypeHash(Effect.CurrentStacks));
	Hash = HashCombine(Hash, GetTypeHash(Effect.Source));
	Hash = HashCombine(Hash, GetTypeHash(Effect.Target));
	Hash = HashCombine(Hash, GetTypeHash(Effect.CommittedAbilityGrants.Num()));
	for (const FSeinEffectAbilityGrant& Grant : Effect.CommittedAbilityGrants)
	{
		Hash = HashCombine(Hash, GetTypeHash(Grant));
	}
	return Hash;
}

template<>
struct TStructOpsTypeTraits<FSeinActiveEffect>
	: public TStructOpsTypeTraitsBase2<FSeinActiveEffect>
{
	enum { WithGetTypeHash = true };
};
