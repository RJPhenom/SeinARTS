/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinSquadBPFL.h
 * @date:		3/27/2026
 * @author:		RJ Macklem
 * @brief:		Blueprint Function Library for squad queries.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "Components/SeinSquadPayload.h"
#include "Components/SeinSquadMemberPayload.h"
#include "SeinSquadBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Squad Library"))
class SEINARTSSQUAD_API USeinSquadBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Read Component Data
	// ====================================================================================================

	/** Read FSeinSquadPayload for an entity. Returns false on invalid handle / missing component. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Data"))
	static bool SeinGetSquadData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinSquadPayload& OutData);

	/** Batch read FSeinSquadPayload. Invalid/missing entities are skipped. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Data"))
	static TArray<FSeinSquadPayload> SeinGetSquadDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

	/** Read FSeinSquadMemberPayload for an entity (member-side, not squad-side). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Member Data"))
	static bool SeinGetSquadMemberData(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FSeinSquadMemberPayload& OutData);

	/** Batch read FSeinSquadMemberPayload. Invalid/missing entities are skipped. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Member Data"))
	static TArray<FSeinSquadMemberPayload> SeinGetSquadMemberDataMany(const UObject* WorldContextObject, const TArray<FSeinEntityHandle>& EntityHandles);

	// Squad Queries
	// ====================================================================================================

	/** Get all members of a squad entity */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Members"))
	static TArray<FSeinEntityHandle> SeinGetSquadMembers(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle);

	/** Get the current leader of a squad */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Leader"))
	static FSeinEntityHandle SeinGetSquadLeader(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle);

	/** Get the squad entity that a member belongs to */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entity Squad"))
	static FSeinEntityHandle SeinGetEntitySquad(const UObject* WorldContextObject, FSeinEntityHandle MemberHandle);

	/** Check whether an entity is a member of any squad */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Is Squad Member"))
	static bool SeinIsSquadMember(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle);

	/** Get the number of alive members in a squad */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Squad", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Squad Size"))
	static int32 SeinGetSquadSize(const UObject* WorldContextObject, FSeinEntityHandle SquadHandle);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
