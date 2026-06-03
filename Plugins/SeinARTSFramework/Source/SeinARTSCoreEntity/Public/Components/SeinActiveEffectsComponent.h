/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActiveEffectsComponent.h
 * @brief   Component that holds all Instance-scope active effects on an entity.
 *          Class- and Player-scope effects live on FSeinPlayerState.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Effects/SeinActiveEffect.h"
#include "Attributes/SeinModifier.h"
#include "GameplayTagContainer.h"
#include "Components/SeinComponent.h"
#include "SeinActiveEffectsComponent.generated.h"

/**
 * ECS-style component storing the Instance-scope active effects on an entity.
 * Class- and Player-scope effects don't live here — they
 * live on the owner's `FSeinPlayerState::ClassEffects` / `::PlayerEffects`.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinActiveEffectsComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** All currently active Instance-scope effects on this entity. */
	UPROPERTY()
	TArray<FSeinActiveEffect> ActiveEffects;

	/** Monotonically increasing ID counter for effect instances (scoped to this
	 *  component — not globally unique, only unique within this entity's storage). */
	uint32 NextEffectInstanceID = 1;

	/** Assign an instance ID and append. Returns the assigned ID. */
	uint32 AddEffect(const FSeinActiveEffect& Effect);

	/** Remove by instance ID. No-op if not found. */
	void RemoveEffect(uint32 EffectInstanceID);

	/** Remove all active effects whose CDO EffectTag matches. */
	void RemoveEffectsWithTag(const FGameplayTag& Tag);

	/** True if any active effect's CDO EffectTag matches. */
	bool HasEffectWithTag(const FGameplayTag& Tag) const;

	/** Sum of CurrentStacks across all active effects whose CDO EffectTag matches. */
	int32 GetStackCountForTag(const FGameplayTag& Tag) const;

	/** Sum of CurrentStacks across all active effects of a given class. */
	int32 GetStackCountForClass(TSubclassOf<USeinEffect> EffectClass) const;

	/** Gather all CDO modifiers from every active effect into OutModifiers.
	 *  Modifiers are emitted once per stack (matches DESIGN §8 `Stack` rule). */
	void CollectModifiers(TArray<FSeinModifier>& OutModifiers) const;

	/** Clear storage. */
	void Clear();
};

FORCEINLINE uint32 GetTypeHash(const FSeinActiveEffectsComponent& Comp)
{
	uint32 Hash = 0;
	for (const FSeinActiveEffect& Effect : Comp.ActiveEffects)
	{
		Hash = HashCombine(Hash, GetTypeHash(Effect));
	}
	return Hash;
}
