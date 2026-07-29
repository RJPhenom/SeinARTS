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

	// Whole-struct setters
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

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Slot", meta = (WorldContext = "WorldContextObject", DisplayName = "Empty Squad Slot"))
	static bool SeinEmptySquadSlot(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Squad|Slot", meta = (WorldContext = "WorldContextObject", DisplayName = "Set Slot Offset Transform"))
	static bool SeinSetSlotOffsetTransform(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle, FGameplayTag SlotTag, FFixedTransform NewOffset);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
