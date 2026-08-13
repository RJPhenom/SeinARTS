/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinContainmentMemberData.h
 * @brief   Back-reference for containable entities. Added at designer time on
 *          entity types that can enter a container; the framework populates
 *          CurrentContainer / CurrentSlot on enter.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinComponent.h"
#include "SeinContainmentMemberData.generated.h"

/**
 * Per-entity containment state. Designers author `Size` on the CDO (e.g.,
 * a 6-man squad contributes 6 to its container's `CurrentLoad`); the
 * framework populates the runtime fields on enter/exit.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinContainmentMemberData : public FSeinComponent
{
	GENERATED_BODY()

	/** Weight contributed to the container's `CurrentLoad`. Default 1;
	 *  larger for squads / multi-occupant entities. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment",
		meta = (ClampMin = "1"))
	int32 Size = 1;

	/** Container currently holding this entity. Invalid = not contained. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	FSeinEntityHandle CurrentContainer;

	/** If the current containment is via an attachment slot, the slot tag
	 *  this entity occupies. Empty for plain containment (transport / garrison). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	FGameplayTag CurrentSlot;

	/** Visual-slot index assigned by `USeinWorldSubsystem::EnterContainer` when
	 *  the container has `bTracksVisualSlots = true`. -1 if tracking is off
	 *  or if the entity isn't currently contained. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	int32 VisualSlotIndex = INDEX_NONE;
};

FORCEINLINE uint32 GetTypeHash(const FSeinContainmentMemberData& Data)
{
	uint32 Hash = GetTypeHash(Data.Size);
	Hash = HashCombine(Hash, GetTypeHash(Data.CurrentContainer));
	Hash = HashCombine(Hash, GetTypeHash(Data.VisualSlotIndex));
	return Hash;
}
