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
	 *  PlayerState). Returns the assigned effect instance ID, or 0 if the apply
	 *  failed or was deferred to the next PreTick via the pending-apply queue. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Effect"))
	static int32 SeinApplyEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle,
		TSubclassOf<USeinEffect> EffectClass, FSeinEntityHandle SourceHandle);

	/** Remove a specific effect instance by its ID. Works across all three scopes —
	 *  the ID is resolved against the target's Instance storage first, then the
	 *  owner's Class and Player lists. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Effect"))
	static void SeinRemoveEffect(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, int32 EffectInstanceID);

	/** Remove every active effect whose CDO EffectTag matches the given tag. Walks
	 *  all three scopes anchored at the target and its owner. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Effects With Tag"))
	static void SeinRemoveEffectsWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag);

	/** True if any Instance-scope active effect on the target's active-effects
	 *  component has a matching EffectTag. (Class/Player-scope checks have
	 *  their own helpers once the use cases surface.) */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Effect With Tag"))
	static bool SeinHasEffectWithTag(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag Tag);

	/** Sum of CurrentStacks across all Instance-scope active effects on the target
	 *  whose CDO EffectTag matches. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Effect",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Effect Stacks"))
	static int32 SeinGetEffectStacks(const UObject* WorldContextObject, FSeinEntityHandle TargetHandle, FGameplayTag EffectTag);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
