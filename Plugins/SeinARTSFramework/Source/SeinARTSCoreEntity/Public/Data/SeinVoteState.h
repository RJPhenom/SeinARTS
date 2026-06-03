/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinVoteState.h
 * @brief   Voting primitive (DESIGN §18). Active-vote storage + resolution modes
 *          + status enum. Sim primitive — lockstep-deterministic.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "SeinVoteState.generated.h"

/**
 * How a vote resolves once all players have voted OR the timer expires.
 * DESIGN §18.
 */
UENUM(BlueprintType)
enum class ESeinVoteResolution : uint8
{
	/** Pass iff >50% of eligible players voted yes. */
	Majority,
	/** Pass iff 100% of eligible players voted yes. */
	Unanimous,
	/** Host's vote decides (single-voter resolution — observer votes ignored). */
	HostDecides,
	/** Pass iff yes > no (simple plurality; ties fail). */
	Plurality,
};

/**
 * Vote status snapshot returned by `USeinVoteBPFL::SeinCheckVoteStatus`.
 */
UENUM(BlueprintType)
enum class ESeinVoteStatus : uint8
{
	NotStarted,
	Active,
	Passed,
	Failed,
};

/**
 * Active vote state. Sim storage on `USeinWorldSubsystem::ActiveVotes`;
 * resolved votes are removed from the map + fire `VoteResolved`.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinVoteState
{
	GENERATED_BODY()

	/** Vote identity tag (e.g., `SeinARTS.Vote.SkipCinematic.Intro`,
	 *  `MyGame.Vote.ConcedeMatch`). Tag semantics are designer-territory. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Vote")
	FGameplayTag VoteType;

	/** Per-player vote value. Framework reads 0 = no, 1 = yes; designers
	 *  may encode richer semantics (they still just bucket yes/no). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Vote")
	TMap<FSeinPlayerID, int32> Votes;

	/** Number of "yes" votes needed to pass. Bypass by setting Resolution. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Vote")
	int32 RequiredThreshold = 1;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Vote")
	ESeinVoteResolution Resolution = ESeinVoteResolution::Majority;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Vote")
	int32 InitiatedAtTick = 0;

	/** Tick at which the vote auto-resolves as Failed if not already passed. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Vote")
	int32 ExpiresAtTick = 0;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Vote")
	FSeinPlayerID Initiator;
};

/**
 * Command payload for `SeinARTS.Command.Type.StartVote`. Carries the
 * parameters `StartVote` would otherwise take as separate args — the
 * command pipeline restricts us to the FSeinCommand common fields +
 * one FInstancedStruct payload per dispatch.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinStartVoteCommandPayload
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Vote")
	ESeinVoteResolution Resolution = ESeinVoteResolution::Majority;

	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Vote")
	int32 RequiredThreshold = 1;

	/** Vote auto-fails after this many sim ticks if not already resolved. Zero
	 *  or negative = no expiration (designer resolves manually). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Vote")
	int32 ExpiresInTicks = 300;
};
