/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatMutationBPFL.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Restricted combat mutations for ability/effect graphs —
 *               firing and scripted damage route through the same
 *               deterministic gates native code uses.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"
#include "Core/SeinEntityHandle.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Types/FixedPoint.h"
#include "SeinCombatMutationBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Combat Mutation Library", RestrictedToClasses = "SeinAbility,SeinEffect"))
class SEINARTSCOMBAT_API USeinCombatMutationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Fire one authored weapon slot at a target through the full gate
	 *  (readiness, range, arc, fog LoS). Returns true when it fired. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Fire Weapon At"))
	static bool SeinFireWeaponAt(
		const UObject* WorldContextObject,
		FSeinEntityHandle Shooter,
		int32 WeaponIndex,
		FSeinEntityHandle Target);

	/** Scripted damage: apply a payload directly (traps, abilities, area
	 *  effects). Instigator may be invalid for environmental damage. Returns
	 *  the damage actually dealt. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Damage"))
	static FFixedPoint SeinApplyDamage(
		const UObject* WorldContextObject,
		FSeinEntityHandle Target,
		FSeinEntityHandle Instigator,
		const FSeinDamagePayload& Payload);

	/** Scripted heal: restore health up to the vitals ceiling. Returns the
	 *  amount actually restored. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Combat",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Apply Heal"))
	static FFixedPoint SeinApplyHeal(
		const UObject* WorldContextObject,
		FSeinEntityHandle Target,
		FSeinEntityHandle Instigator,
		FFixedPoint Amount);
};
