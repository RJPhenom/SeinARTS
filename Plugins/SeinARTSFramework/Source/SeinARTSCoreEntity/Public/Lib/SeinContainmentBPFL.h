/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinContainmentBPFL.h
 * @brief   BP surface for the Relationships primitive (DESIGN §14).
 *          Wraps USeinWorldSubsystem's containment API + exposes introspection
 *          (immediate / root container, occupants, nested occupants, tree).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Components/SeinContainmentTypes.h"
#include "SeinContainmentBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Containment Library"))
class SEINARTSCOREENTITY_API USeinContainmentBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Mutations — these forward to USeinWorldSubsystem's authoritative helpers.
	// ====================================================================================================

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Enter Container"))
	static bool SeinEnterContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinEntityHandle Container);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Exit Container",
			AdvancedDisplay = "ExitLocation"))
	static bool SeinExitContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity, FFixedVector ExitLocation);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Attach To Slot"))
	static bool SeinAttachToSlot(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinEntityHandle Container, FGameplayTag SlotTag);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Detach From Slot"))
	static bool SeinDetachFromSlot(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	// Reads — BlueprintPure where idempotent.
	// ====================================================================================================

	/** Read FSeinContainmentData for a container. False / untouched on missing component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Containment Data"))
	static bool SeinGetContainmentData(const UObject* WorldContextObject, FSeinEntityHandle Container, FSeinContainmentData& OutData);

	/** Read FSeinContainmentMemberData for a member. False / untouched on missing component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Containment Member Data"))
	static bool SeinGetContainmentMemberData(const UObject* WorldContextObject, FSeinEntityHandle Entity, FSeinContainmentMemberData& OutData);

	/** Direct parent (or invalid if not contained). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Immediate Container"))
	static FSeinEntityHandle SeinGetImmediateContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Outermost container via parent-chain walk (depth-capped). Returns invalid if not contained. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Root Container"))
	static FSeinEntityHandle SeinGetRootContainer(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** True iff Entity is currently inside any container. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Contained"))
	static bool SeinIsContained(const UObject* WorldContextObject, FSeinEntityHandle Entity);

	/** Immediate occupant list (direct children only). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Occupants"))
	static TArray<FSeinEntityHandle> SeinGetOccupants(const UObject* WorldContextObject, FSeinEntityHandle Container);

	/** Flat list of every nested descendant (depth-first, pre-order). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get All Nested Occupants"))
	static TArray<FSeinEntityHandle> SeinGetAllNestedOccupants(const UObject* WorldContextObject, FSeinEntityHandle Container);

	/** Flattened depth-first containment tree starting at Container. Pre-ordered
	 *  entries with per-node Depth + ParentIndex; BP consumers walk sequentially. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Containment Tree"))
	static FSeinContainmentTree SeinGetContainmentTree(const UObject* WorldContextObject, FSeinEntityHandle Container);

	/** Entity currently filling a container's named attachment slot, or invalid. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Slot Occupant"))
	static FSeinEntityHandle SeinGetSlotOccupant(const UObject* WorldContextObject, FSeinEntityHandle Container, FGameplayTag SlotTag);

	/** Pre-validates whether Container would accept Entity right now. Mirrors the
	 *  gates EnterContainer runs (capacity + Size + tag query). Does NOT attach. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Containment",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Can Accept Entity"))
	static bool SeinCanAcceptEntity(const UObject* WorldContextObject, FSeinEntityHandle Container, FSeinEntityHandle Entity);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
