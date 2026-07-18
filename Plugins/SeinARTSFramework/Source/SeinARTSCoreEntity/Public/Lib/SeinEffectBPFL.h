/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinEffectBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for applying and removing
 *				`USeinEffect` instances across all three DESIGN §8 scopes.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Effects/SeinEffect.h"
#include "SeinEffectBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Effect Library"))
class SEINARTSCOREENTITY_API USeinEffectBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Apply a `USeinEffect` class to a target. Scope from the effect CDO determines
	 *  storage location (Instance on target, Class / Player on target owner's
	 *  PlayerState). Returns the world-global effect instance ID, or 0 if the apply
	 *  failed/was deferred to the next PreTick. Deferred zero is not a reservation:
	 *  stacking and validity resolve only when that queue drains. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Effect"))
	static int64 SeinApplyEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle,
		TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle SourceHandle);

	/** Target convenience for removing a specific effect instance by ID. The live
	 *  target selects its Instance storage and owning player's Class/Player lists;
	 *  use Remove Effect by ID when no live target is available. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Effect"))
	static void SeinRemoveEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, int64 EffectInstanceID);

	/** Remove a specific effect using its world-global ID alone. Returns false
	 *  when no active effect owns that ID. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Effect by ID"))
	static bool SeinRemoveEffectByID(const UObject* WorldContextObject, int64 EffectInstanceID);

	/** Remove every active effect whose CDO EffectTag matches the given tag. Walks
	 *  all three scopes anchored at the target and its owner. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Effects With Tag"))
	static void SeinRemoveEffectsWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag);

	/** True if any Instance-scope active effect on the target has a matching
	 *  EffectTag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Effect With Tag"))
	static bool SeinHasEffectWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag);

	/** Sum of CurrentStacks across all Instance-scope active effects on the target
	 *  whose CDO EffectTag matches. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Effect Stacks"))
	static int32 SeinGetEffectStacks(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag EffectTag);

	/** Query the Class- or Player-scope storage owned by PlayerID. Instance is
	 *  invalid without an entity target and deterministically returns false. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Effect With Tag for Player"))
	static bool SeinHasEffectWithTagForPlayer(const UObject* WorldContextObject, FSeinPlayerID PlayerID,
		FGameplayTag EffectTag, ESeinModifierScope Scope);

	/** Sum stacks in the selected Class- or Player-scope storage. Instance is
	 *  invalid without an entity target and deterministically returns zero. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Effect Stacks for Player"))
	static int32 SeinGetEffectStacksForPlayer(const UObject* WorldContextObject, FSeinPlayerID PlayerID,
		FGameplayTag EffectTag, ESeinModifierScope Scope);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
