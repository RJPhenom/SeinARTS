/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVoteBPFL.h
 * @brief   BP surface for the voting primitive (DESIGN §18). Start / cast
 *          via the command buffer (lockstep-deterministic); read via direct
 *          subsystem queries.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Data/SeinVoteState.h"
#include "SeinVoteBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Vote Library"))
class SEINARTSCOREENTITY_API USeinVoteBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Enqueue a StartVote command. The sim processes it next tick, creating
	 *  an entry in `ActiveVotes` and firing `VoteStarted`. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Vote",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Start Vote"))
	static void SeinStartVote(const UObject* WorldContextObject,
		FGameplayTag VoteType,
		ESeinVoteResolution Resolution,
		int32 RequiredThreshold,
		int32 ExpiresInTicks,
		FSeinPlayerID Initiator);

	/** Enqueue a CastVote command. `VoteValue`: 0 = no, 1 = yes. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Vote",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Cast Vote"))
	static void SeinCastVote(const UObject* WorldContextObject, FGameplayTag VoteType, FSeinPlayerID Voter, int32 VoteValue);

	/** Snapshot of all active votes for UI listing. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Vote",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Active Votes"))
	static TArray<FSeinVoteState> SeinGetActiveVotes(const UObject* WorldContextObject);

	/** Status of a specific vote — NotStarted / Active / Passed / Failed.
	 *  V1 reports NotStarted for both "never started" and "resolved" (resolved
	 *  votes are drained from `ActiveVotes`). Subscribers use the `VoteResolved`
	 *  visual event to learn the outcome. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Vote",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Check Vote Status"))
	static ESeinVoteStatus SeinCheckVoteStatus(const UObject* WorldContextObject, FGameplayTag VoteType);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
