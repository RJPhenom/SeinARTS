/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadMutationBPFL.h
 * @brief   Restricted-access Blueprint Function Library for mutating squad
 *          sim-side component state. Mirrors USeinSimMutationBPFL's contract:
 *          callable only from USeinAbility and USeinEffect Blueprint graphs,
 *          with an all-build world gate that permits tick-zero Applying and
 *          fixed-tick callbacks but rejects post-seal/off-tick mutation.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Transform.h"
#include "GameplayTagContainer.h"
#include "Components/SeinSquadComponent.h"
#include "Components/SeinSquadMemberComponent.h"
#include "SeinSquadMutationBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Squad Mutation Library", RestrictedToClasses = "SeinAbility,SeinEffect"))
class SEINARTSSQUAD_API USeinSquadMutationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Legacy whole-struct setters. These are restricted to uninitialized,
	// unassigned tick-zero data; live topology routes through exact APIs below.
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Squad Data"))
	static bool SeinSetSquadData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinSquadComponent& NewData);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Squad Member Data"))
	static bool SeinSetSquadMemberData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, const FSeinSquadMemberComponent& NewData);

	// Field-level setters
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Squad Leader"))
	static bool SeinSetSquadLeader(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewLeader);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Add Squad Member"))
	static bool SeinAddSquadMember(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FSeinEntityHandle NewMember);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Squad Member"))
	static bool SeinRemoveSquadMember(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FSeinEntityHandle MemberToRemove);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Slot", meta = (WorldContext = "WorldContextObject", DisplayName = "Fill Squad Slot"))
	static bool SeinFillSquadSlot(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag, FSeinEntityHandle Member);

	/** Exact identity variant. Prefer this when role tags may be shared. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Slot", meta = (WorldContext = "WorldContextObject", DisplayName = "Fill Squad Slot by Index"))
	static bool SeinFillSquadSlotByIndex(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, int32 SlotIndex, FSeinEntityHandle Member);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Slot", meta = (WorldContext = "WorldContextObject", DisplayName = "Empty Squad Slot"))
	static bool SeinEmptySquadSlot(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag);

	/** Exact identity variant. Prefer this when role tags may be shared. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Slot", meta = (WorldContext = "WorldContextObject", DisplayName = "Empty Squad Slot by Index"))
	static bool SeinEmptySquadSlotByIndex(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, int32 SlotIndex);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Slot", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Slot Offset Transform"))
	static bool SeinSetSlotOffsetTransform(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag, FFixedTransform NewOffset);

	/** Queue one exact slot and return its monotonic request identity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Reinforcement", meta = (WorldContext = "WorldContextObject", DisplayName = "Queue Squad Reinforcement"))
	static bool SeinQueueSquadReinforcement(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, int32 SlotIndex, int64& OutRequestID);

	/** Cancel one exact request and refund its snapshotted payer/cost. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Reinforcement", meta = (WorldContext = "WorldContextObject", DisplayName = "Cancel Squad Reinforcement"))
	static bool SeinCancelSquadReinforcement(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, int64 RequestID);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
