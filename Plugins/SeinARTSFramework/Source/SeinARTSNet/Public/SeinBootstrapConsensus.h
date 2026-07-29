/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinBootstrapConsensus.h
 * @brief Pure topology-neutral consensus over tick-zero materialization receipts.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinNetProtocolTypes.h"
#include "Simulation/SeinMatchBootstrapBarrier.h"

/** Result of installing one immutable bootstrap-consensus membership. */
enum class ESeinBootstrapConsensusConfigResult : uint8
{
	Configured,
	InvalidContext,
	NoRequiredParticipants,
	TooManyRequiredParticipants,
	InvalidParticipant,
	DuplicateParticipant,
};

/** Persistent lifecycle of one configured bootstrap-consensus attempt. */
enum class ESeinBootstrapConsensusState : uint8
{
	Unconfigured,
	CollectingReceipts,
	ReceiptAgreed,
	CollectingAuthorizedReady,
	AuthorizedReady,
	Launched,
	Failed,
};

/** Retained terminal failure reason; evidence remains available until Reset. */
enum class ESeinBootstrapConsensusFailure : uint8
{
	None,
	ConflictingRetry,
	ReceiptDisagreement,
	ContractDigestMismatch,
	SimulationContentDigestMismatch,
	PhaseReceiptMismatch,
};

/** Result of one authenticated participant's receipt submission. */
enum class ESeinBootstrapConsensusSubmitResult : uint8
{
	Accepted,
	IdenticalRetry,
	AgreementReached,
	AuthorizationReady,
	InvalidContext,
	InvalidParticipant,
	UnexpectedParticipant,
	InvalidReceipt,
	ContractDigestMismatch,
	SimulationContentDigestMismatch,
	InvalidPhase,
	AlreadyAgreed,
	AlreadyFailed,
	ConflictingRetry,
	ReceiptDisagreement,
};

/**
 * Bounded, transport-independent gather for one match's tick-zero receipts.
 *
 * The caller supplies an authenticated participant identity; no participant
 * identity is accepted from receipt payload data. The first accepted receipt
 * becomes immutable. Identical retries are idempotent, while either participant
 * equivocation or disagreement between participants fails the attempt
 * terminally. A new exact context requires a fresh Configure call.
 *
 * RequiredParticipants is the set of processes that actually simulate. It need
 * not include a dedicated coordinator that does not simulate. Callers derive it
 * from the already-validated membership manifest; this primitive cannot recover
 * that manifest from FSeinProtocolContext::MembershipDigest.
 */
class SEINARTSNET_API FSeinBootstrapConsensus
{
public:
	/**
	 * Replace the current attempt after fully validating the candidate config.
	 * Invalid configuration leaves any existing attempt untouched.
	 */
	ESeinBootstrapConsensusConfigResult Configure(
		const FSeinProtocolContext& InContext,
		const TArray<FSeinNetworkParticipantID>& InRequiredParticipants);

	/**
	 * Submit one locally materialized receipt from an identity authenticated by
	 * the active transport. MessageContext must equal the configured context in
	 * every field; coordinator-term advancement therefore requires reconfigure.
	 */
	ESeinBootstrapConsensusSubmitResult Submit(
		const FSeinProtocolContext& MessageContext,
		FSeinNetworkParticipantID AuthenticatedParticipant,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Enter the authorization-ACK gather after receipt agreement. Idempotent. */
	bool BeginAuthorization();

	ESeinBootstrapConsensusSubmitResult SubmitAuthorizedReady(
		const FSeinProtocolContext& MessageContext,
		FSeinNetworkParticipantID AuthenticatedParticipant,
		const FSeinMatchBootstrapReceipt& Receipt);

	/** Commit launch after unanimous authorization readiness. Idempotent. */
	bool BeginLaunch();

	void Reset();

	bool IsConfigured() const
	{
		return State != ESeinBootstrapConsensusState::Unconfigured;
	}

	ESeinBootstrapConsensusState GetState() const { return State; }
	ESeinBootstrapConsensusFailure GetFailure() const { return Failure; }
	const FSeinProtocolContext& GetContext() const { return Context; }

	/** Stable GUID-component order, independent of caller/transport order. */
	const TArray<FSeinNetworkParticipantID>& GetRequiredParticipants() const
	{
		return RequiredParticipants;
	}

	/** Receipt evidence is retained through launch commit or failure. */
	int32 GetSubmittedParticipantCount() const { return SubmittedReceipts.Num(); }
	int32 GetAuthorizedReadyParticipantCount() const
	{
		return AuthorizedReadyReceipts.Num();
	}

	/** Missing participants for the currently collecting phase, in canonical order. */
	TArray<FSeinNetworkParticipantID> GetMissingParticipants() const;

	/** Agreed evidence remains readable until Reset, including after failure. */
	bool GetAgreedReceipt(FSeinMatchBootstrapReceipt& OutReceipt) const;
	bool IsLaunchInFlight() const;
	bool IsLaunchComplete() const
	{
		return State == ESeinBootstrapConsensusState::Launched;
	}

private:
	static bool ParticipantLess(
		const FSeinNetworkParticipantID& A,
		const FSeinNetworkParticipantID& B);

	ESeinBootstrapConsensusSubmitResult FailTerminal(
		ESeinBootstrapConsensusFailure InFailure,
		ESeinBootstrapConsensusSubmitResult Result);
	bool ValidateSubmissionEnvelope(
		const FSeinProtocolContext& MessageContext,
		FSeinNetworkParticipantID AuthenticatedParticipant,
		const FSeinMatchBootstrapReceipt& Receipt,
		ESeinBootstrapConsensusSubmitResult& OutFailureResult);
	ESeinBootstrapConsensusSubmitResult SubmitPhaseEvidence(
		const FSeinProtocolContext& MessageContext,
		FSeinNetworkParticipantID AuthenticatedParticipant,
		const FSeinMatchBootstrapReceipt& Receipt,
		ESeinBootstrapConsensusState CollectingState,
		ESeinBootstrapConsensusState CompletedState,
		ESeinBootstrapConsensusSubmitResult CompletionResult,
		TMap<FSeinNetworkParticipantID, FSeinMatchBootstrapReceipt>& Evidence);

	ESeinBootstrapConsensusState State =
		ESeinBootstrapConsensusState::Unconfigured;
	ESeinBootstrapConsensusFailure Failure =
		ESeinBootstrapConsensusFailure::None;
	FSeinProtocolContext Context;
	TArray<FSeinNetworkParticipantID> RequiredParticipants;
	TSet<FSeinNetworkParticipantID> RequiredParticipantSet;
	TMap<FSeinNetworkParticipantID, FSeinMatchBootstrapReceipt> SubmittedReceipts;
	TMap<FSeinNetworkParticipantID, FSeinMatchBootstrapReceipt>
		AuthorizedReadyReceipts;
	TOptional<FSeinMatchBootstrapReceipt> CandidateReceipt;
	TOptional<FSeinMatchBootstrapReceipt> AgreedReceipt;
};
