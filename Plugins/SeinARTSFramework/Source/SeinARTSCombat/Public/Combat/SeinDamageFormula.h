/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinDamageFormula.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Damage formula policy seam.
 *
 *          The framework owns damage ORDERING and application; the formula
 *          owns the ARITHMETIC — armor interactions, facing modifiers, cover
 *          bonuses, splash falloff, anything that defines a game's combat
 *          feel. Stateless CDO policy (the formation/scorer pattern): the
 *          class is resolved from the payload's soft path and its default
 *          object computes; no instances, no retained state. Deterministic
 *          inputs only — the context is fixed-point and tag data.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "UObject/Object.h"
#include "SeinDamageFormula.generated.h"

/** Deterministic inputs handed to a damage formula evaluation. */
USTRUCT(BlueprintType)
struct SEINARTSCOMBAT_API FSeinDamageContext
{
	GENERATED_BODY()

	/** Entity being damaged. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FSeinEntityHandle Target;

	/** Entity credited with the damage (may be invalid for scripted damage). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FSeinEntityHandle Instigator;

	/** The authored payload being delivered. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FSeinDamagePayload Payload;

	/** The target's armor-class tag (Combat.Armor.None when unauthored). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FGameplayTag TargetArmorTag;

	/** Planar distance from the impact point (nonzero only for splash). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedPoint DistanceFromImpact = FFixedPoint::Zero;
};

/**
 * Abstract damage formula. Subclass in Blueprint (or C++) and reference the
 * class from a weapon's damage payload. Runs on the class default object with
 * a fully deterministic context; the returned value is clamped at zero by the
 * caller. Read additional target/instigator component state through the typed
 * component get nodes if a formula needs it (cover tags, veterancy, facing).
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Damage Formula"))
class SEINARTSCOMBAT_API USeinDamageFormula : public UObject
{
	GENERATED_BODY()

public:
	/** Compute final damage from the context. Pure policy — no side effects. */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "SeinARTS|Combat")
	FFixedPoint ComputeDamage(const FSeinDamageContext& Context) const;
	virtual FFixedPoint ComputeDamage_Implementation(
		const FSeinDamageContext& Context) const
	{
		return Context.Payload.BaseDamage;
	}
};
