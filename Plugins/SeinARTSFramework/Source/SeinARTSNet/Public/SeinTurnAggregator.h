/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinTurnAggregator.h
 * @brief Pure topology-neutral lockstep turn gather and canonical assembly.
 */

#pragma once

#include "CoreMinimal.h"
#include "Input/SeinCommand.h"
#include "SeinNetProtocolTypes.h"

enum class ESeinTurnAggregatorConfigResult : uint8
{
	Configured,
	InvalidContext,
	InvalidBinding,
	DuplicateParticipant,
	DuplicateCommandSlot,
	TooManyCommandAuthors,
	TooManyParticipants,
	MembershipDigestMismatch,
	InvalidCoordinator,
};

enum class ESeinCoordinatorTermAdvanceResult : uint8
{
	Advanced,
	NotConfigured,
	InvalidContext,
	IncompatibleContext,
	TermNotNewer,
	InvalidCoordinator,
};

enum class ESeinTurnSubmitResult : uint8
{
	Accepted,
	IdenticalRetry,
	ConflictingRetry,
	InvalidContext,
	InvalidTurn,
	UnexpectedAuthor,
	TurnCommitted,
	TurnRetired,
	AggregateRejected,
};

enum class ESeinTurnCommitResult : uint8
{
	Committed,
	NotReady,
	InvalidContext,
	InvalidTurn,
	AlreadyCommitted,
	TurnRetired,
};

/**
 * Collects immutable per-author submissions and emits one canonical turn.
 * It owns no UObject and has no world, actor, NetMode, transport, or
 * coordinator-election dependency. Callers authenticate participants and
 * validate/stamp command contents before submission.
 */
class SEINARTSNET_API FSeinTurnAggregator
{
public:
	ESeinTurnAggregatorConfigResult Configure(
		const FSeinProtocolContext& InContext,
		const TArray<FSeinParticipantBinding>& Bindings);

	/** Preserve the complete ledger while moving authority to a newer term. */
	ESeinCoordinatorTermAdvanceResult AdvanceCoordinatorTerm(
		const FSeinProtocolContext& NewContext);

	ESeinTurnSubmitResult Submit(
		const FSeinProtocolContext& MessageContext,
		int32 TurnID,
		const FSeinTurnAuthor& Author,
		const TArray<FSeinCommand>& Commands);

	/**
	 * Before accepting every first submission, assemble all present authors plus
	 * the candidate in canonical order and invoke CanAcceptProspectiveTurn. A
	 * false result leaves the candidate unrecorded/retryable, so an early batch
	 * cannot consume the fan-out budget and strand a later empty heartbeat.
	 */
	ESeinTurnSubmitResult Submit(
		const FSeinProtocolContext& MessageContext,
		int32 TurnID,
		const FSeinTurnAuthor& Author,
		const TArray<FSeinCommand>& Commands,
		TFunctionRef<bool(TConstArrayView<FSeinCommand>)> CanAcceptProspectiveTurn);

	/** Commit iff every configured author has submitted. Output is always reset. */
	ESeinTurnCommitResult TryCommit(
		const FSeinProtocolContext& MessageContext,
		int32 TurnID,
		TArray<FSeinCommand>& OutCommands);

	bool IsConfigured() const { return bConfigured; }
	const FSeinProtocolContext& GetContext() const { return Context; }
	const TArray<FSeinTurnAuthor>& GetExpectedAuthors() const { return ExpectedAuthors; }
	/** Committed turns report every expected author as submitted until pruned. */
	bool HasSubmission(int32 TurnID, const FSeinTurnAuthor& Author) const;
	int32 GetSubmittedAuthorCount(int32 TurnID) const;
	/** Stable canonical order; empty for committed, retired, or invalid turns. */
	TArray<FSeinTurnAuthor> GetMissingAuthors(int32 TurnID) const;
	/** Sorted IDs for pending turns; read-only recovery/diagnostic view. */
	TArray<int32> GetPendingTurnIDs() const;
	bool IsTurnCommitted(int32 TurnID) const { return CommittedTurns.Contains(TurnID); }
	int32 GetRetainedCommittedTurnCount() const { return CommittedTurns.Num(); }
	bool IsTurnRetired(int32 TurnID) const { return TurnID >= 0 && TurnID <= TurnRejectionFloor; }
	int32 GetTurnRejectionFloor() const { return TurnRejectionFloor; }

	/**
	 * Discard pending and committed storage through InclusiveTurn while keeping
	 * a rejection floor so obsolete turns can never be submitted again.
	 */
	void PruneThroughTurn(int32 InclusiveTurn);
	void Reset();

private:
	struct FPendingTurn
	{
		TMap<FSeinTurnAuthor, TArray<FSeinCommand>> Batches;
	};

	bool IsExpectedAuthor(const FSeinTurnAuthor& Author) const;
	static bool AreBatchesIdentical(
		const TArray<FSeinCommand>& A,
		const TArray<FSeinCommand>& B);

	bool bConfigured = false;
	FSeinProtocolContext Context;
	TArray<FSeinTurnAuthor> ExpectedAuthors;
	TSet<FSeinTurnAuthor> ExpectedAuthorSet;
	TSet<FSeinNetworkParticipantID> EligibleCoordinators;
	TMap<int32, FPendingTurn> PendingTurns;
	TSet<int32> CommittedTurns;
	int32 TurnRejectionFloor = INDEX_NONE;
};
