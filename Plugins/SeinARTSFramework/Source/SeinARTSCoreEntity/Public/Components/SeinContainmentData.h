/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinContainmentData.h
 * @brief   Container component (DESIGN §14). Hybrid-primitive base — adopt
 *          one of the spec companions (attachment / transport / garrison)
 *          to layer on type-specific behavior.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinComponent.h"
#include "Components/SeinContainmentTypes.h"
#include "SeinContainmentData.generated.h"

class USeinEffect;

/**
 * Container state — occupant list, capacity, visibility mode, destruction
 * behavior. One container may additionally carry `FSeinAttachmentSpec`,
 * `FSeinTransportSpec`, `FSeinGarrisonSpec` for specialization.
 *
 * Members are not reassigned to the container's owner on enter — they keep
 * their original `FSeinPlayerID` so stats / pop accounting follows each
 * entity's true owner (DESIGN §14).
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSCOREENTITY_API FSeinContainmentData : public FSeinComponent
{
	GENERATED_BODY()

	/** Live occupant handles. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	TArray<FSeinEntityHandle> Occupants;

	/** Maximum total occupancy "weight" the container accepts. Each occupant
	 *  contributes its `FSeinContainmentMemberData::Size` (default 1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment",
		meta = (ClampMin = "0"))
	int32 TotalCapacity = 1;

	/** Sum of occupants' Size values. Must stay ≤ TotalCapacity. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	int32 CurrentLoad = 0;

	/** Tag query an entity must satisfy to be accepted. Empty = any entity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment")
	FGameplayTagQuery AcceptedEntityQuery;

	/** If true: on container death, eject occupants at the container's last
	 *  position and apply `OnEjectEffect` (if set). If false: occupants die
	 *  with the container and `OnContainerDeathEffect` (if set) fires for each. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment")
	bool bEjectOnContainerDeath = true;

	/** Applied to each occupant when the container dies with eject=true. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment",
		meta = (EditCondition = "bEjectOnContainerDeath"))
	TSubclassOf<USeinEffect> OnEjectEffect;

	/** Applied to each occupant when the container dies with eject=false, just
	 *  before the occupant is destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment",
		meta = (EditCondition = "!bEjectOnContainerDeath"))
	TSubclassOf<USeinEffect> OnContainerDeathEffect;

	/** How occupants interact with sim spatial queries + vision while contained. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment")
	ESeinContainmentVisibility Visibility = ESeinContainmentVisibility::Hidden;

	/** Opt-in: framework tracks deterministic visual slot assignments — which
	 *  occupant is visible at which window/hatch index. Render side reads
	 *  `VisualSlotAssignments[i]` to place that entity at visual slot `i`.
	 *  V1 assignment is first-free-slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Containment")
	bool bTracksVisualSlots = false;

	/** When `bTracksVisualSlots == true`, sized to `TotalCapacity`. Free slot
	 *  index holds an invalid handle. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Containment")
	TArray<FSeinEntityHandle> VisualSlotAssignments;
};

FORCEINLINE uint32 GetTypeHash(const FSeinContainmentData& Data)
{
	uint32 Hash = GetTypeHash(Data.TotalCapacity);
	Hash = HashCombine(Hash, GetTypeHash(Data.CurrentLoad));
	Hash = HashCombine(Hash, GetTypeHash(Data.Occupants.Num()));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Data.Visibility)));
	return Hash;
}
