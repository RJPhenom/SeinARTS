/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGarrisonSpec.h
 * @brief   Garrison specialization spec (DESIGN §14). Optional — layer on
 *          `FSeinContainmentData` for buildings whose occupants fire out
 *          of designer-declared windows/firing slots.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/SeinComponent.h"
#include "SeinGarrisonSpec.generated.h"

/**
 * Per-container garrison metadata. Ships empty-ish — designer authors the
 * `FiringSlotTags` list against their own `SeinARTS.Slot.*` vocabulary,
 * and the starter `Ability_GarrisonFire` BP reads these to decide where
 * garrisoned infantry fire out of. Framework never interprets the tags.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinGarrisonSpec : public FSeinComponent
{
	GENERATED_BODY()

	/** Designer-authored firing slot tag list (e.g.,
	 *  SeinARTS.Slot.NorthWindow, .SouthWindow). Consumed by garrison-fire
	 *  abilities; framework just stores the list. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Garrison")
	TArray<FGameplayTag> FiringSlotTags;
};

FORCEINLINE uint32 GetTypeHash(const FSeinGarrisonSpec& Spec)
{
	return GetTypeHash(Spec.FiringSlotTags.Num());
}
