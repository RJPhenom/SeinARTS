/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAttachmentSpec.h
 * @brief   Attachment-specialization spec (DESIGN §14). Layer on
 *          `FSeinContainmentData` when the container has named slots
 *          (driver/gunner/passenger, hero-in-regiment, mounted crew).
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinComponent.h"
#include "Components/SeinContainmentTypes.h"
#include "SeinAttachmentSpec.generated.h"

/**
 * Named-slot layout for attachment containers. `Slots` defines the authored
 * slots; `Assignments` is the runtime map from slot tag to the occupant
 * currently filling it (invalid handle if unfilled).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinAttachmentSpec : public FSeinComponent
{
	GENERATED_BODY()

	/** Authored slot list. Each slot has a tag, local offset, and optional
	 *  accepted-entity tag query. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Attachment")
	TArray<FSeinAttachmentSlotDef> Slots;

	/** Runtime slot-to-occupant map. Populated / cleared by
	 *  `USeinWorldSubsystem::AttachToSlot` / `DetachFromSlot`. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Attachment")
	TMap<FGameplayTag, FSeinEntityHandle> Assignments;
};

FORCEINLINE uint32 GetTypeHash(const FSeinAttachmentSpec& Spec)
{
	uint32 Hash = GetTypeHash(Spec.Slots.Num());
	Hash = HashCombine(Hash, GetTypeHash(Spec.Assignments.Num()));
	return Hash;
}
