/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinBootstrapConsensus.cpp
 */

#include "SeinBootstrapConsensus.h"

ESeinBootstrapConsensusConfigResult FSeinBootstrapConsensus::Configure(
	const FSeinProtocolContext& InContext,
	const TArray<FSeinNetworkParticipantID>& InRequiredParticipants)
{
	if (!InContext.IsValid())
	{
		return ESeinBootstrapConsensusConfigResult::InvalidContext;
	}
	if (InRequiredParticipants.IsEmpty())
	{
		return ESeinBootstrapConsensusConfigResult::NoRequiredParticipants;
	}
	if (InRequiredParticipants.Num() > SeinNetProtocolLimits::MaxParticipants)
	{
		return ESeinBootstrapConsensusConfigResult::TooManyRequiredParticipants;
	}

	TArray<FSeinNetworkParticipantID> NewRequiredParticipants =
		InRequiredParticipants;
	TSet<FSeinNetworkParticipantID> NewRequiredParticipantSet;
	NewRequiredParticipantSet.Reserve(NewRequiredParticipants.Num());
	for (const FSeinNetworkParticipantID Participant : NewRequiredParticipants)
	{
		if (!Participant.IsValid())
		{
			return ESeinBootstrapConsensusConfigResult::InvalidParticipant;
		}
		if (NewRequiredParticipantSet.Contains(Participant))
		{
			return ESeinBootstrapConsensusConfigResult::DuplicateParticipant;
		}
		NewRequiredParticipantSet.Add(Participant);
	}
	NewRequiredParticipants.Sort(&FSeinBootstrapConsensus::ParticipantLess);

	Context = InContext;
	RequiredParticipants = MoveTemp(NewRequiredParticipants);
	RequiredParticipantSet = MoveTemp(NewRequiredParticipantSet);
	SubmittedReceipts.Reset();
	AuthorizedReadyReceipts.Reset();
	CandidateReceipt.Reset();
	AgreedReceipt.Reset();
	Failure = ESeinBootstrapConsensusFailure::None;
	State = ESeinBootstrapConsensusState::CollectingReceipts;
	return ESeinBootstrapConsensusConfigResult::Configured;
}

ESeinBootstrapConsensusSubmitResult FSeinBootstrapConsensus::Submit(
	const FSeinProtocolContext& MessageContext,
	FSeinNetworkParticipantID AuthenticatedParticipant,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	if (State == ESeinBootstrapConsensusState::Failed)
	{
		return ESeinBootstrapConsensusSubmitResult::AlreadyFailed;
	}
	if (State == ESeinBootstrapConsensusState::Launched)
	{
		return ESeinBootstrapConsensusSubmitResult::InvalidPhase;
	}
	ESeinBootstrapConsensusSubmitResult ValidationResult;
	if (!ValidateSubmissionEnvelope(
		MessageContext, AuthenticatedParticipant, Receipt, ValidationResult))
	{
		return ValidationResult;
	}

	if (const FSeinMatchBootstrapReceipt* Existing =
		SubmittedReceipts.Find(AuthenticatedParticipant))
	{
		if (*Existing == Receipt)
		{
			return ESeinBootstrapConsensusSubmitResult::IdenticalRetry;
		}
		return FailTerminal(
			ESeinBootstrapConsensusFailure::ConflictingRetry,
			ESeinBootstrapConsensusSubmitResult::ConflictingRetry);
	}
	if (State != ESeinBootstrapConsensusState::CollectingReceipts)
	{
		return AgreedReceipt.IsSet()
			? ESeinBootstrapConsensusSubmitResult::AlreadyAgreed
			: ESeinBootstrapConsensusSubmitResult::InvalidPhase;
	}

	if (CandidateReceipt.IsSet() && CandidateReceipt.GetValue() != Receipt)
	{
		return FailTerminal(
			ESeinBootstrapConsensusFailure::ReceiptDisagreement,
			ESeinBootstrapConsensusSubmitResult::ReceiptDisagreement);
	}
	if (!CandidateReceipt.IsSet())
	{
		CandidateReceipt = Receipt;
	}
	SubmittedReceipts.Add(AuthenticatedParticipant, Receipt);

	if (SubmittedReceipts.Num() != RequiredParticipants.Num())
	{
		return ESeinBootstrapConsensusSubmitResult::Accepted;
	}
	for (const FSeinNetworkParticipantID Required : RequiredParticipants)
	{
		if (!SubmittedReceipts.Contains(Required))
		{
			return ESeinBootstrapConsensusSubmitResult::Accepted;
		}
	}

	check(CandidateReceipt.IsSet());
	AgreedReceipt = CandidateReceipt.GetValue();
	CandidateReceipt.Reset();
	Failure = ESeinBootstrapConsensusFailure::None;
	State = ESeinBootstrapConsensusState::ReceiptAgreed;
	return ESeinBootstrapConsensusSubmitResult::AgreementReached;
}

bool FSeinBootstrapConsensus::BeginAuthorization()
{
	if (State == ESeinBootstrapConsensusState::ReceiptAgreed)
	{
		State = ESeinBootstrapConsensusState::CollectingAuthorizedReady;
		return true;
	}
	return State == ESeinBootstrapConsensusState::CollectingAuthorizedReady
		|| State == ESeinBootstrapConsensusState::AuthorizedReady
		|| State == ESeinBootstrapConsensusState::Launched;
}

ESeinBootstrapConsensusSubmitResult
FSeinBootstrapConsensus::SubmitAuthorizedReady(
	const FSeinProtocolContext& MessageContext,
	FSeinNetworkParticipantID AuthenticatedParticipant,
	const FSeinMatchBootstrapReceipt& Receipt)
{
	return SubmitPhaseEvidence(
		MessageContext,
		AuthenticatedParticipant,
		Receipt,
		ESeinBootstrapConsensusState::CollectingAuthorizedReady,
		ESeinBootstrapConsensusState::AuthorizedReady,
		ESeinBootstrapConsensusSubmitResult::AuthorizationReady,
		AuthorizedReadyReceipts);
}

bool FSeinBootstrapConsensus::BeginLaunch()
{
	if (State == ESeinBootstrapConsensusState::AuthorizedReady)
	{
		State = ESeinBootstrapConsensusState::Launched;
		return true;
	}
	return State == ESeinBootstrapConsensusState::Launched;
}

bool FSeinBootstrapConsensus::ValidateSubmissionEnvelope(
	const FSeinProtocolContext& MessageContext,
	FSeinNetworkParticipantID AuthenticatedParticipant,
	const FSeinMatchBootstrapReceipt& Receipt,
	ESeinBootstrapConsensusSubmitResult& OutFailureResult)
{
	if (State == ESeinBootstrapConsensusState::Unconfigured
		|| MessageContext != Context)
	{
		OutFailureResult = ESeinBootstrapConsensusSubmitResult::InvalidContext;
		return false;
	}
	if (!AuthenticatedParticipant.IsValid())
	{
		OutFailureResult = ESeinBootstrapConsensusSubmitResult::InvalidParticipant;
		return false;
	}
	if (!RequiredParticipantSet.Contains(AuthenticatedParticipant))
	{
		OutFailureResult = ESeinBootstrapConsensusSubmitResult::UnexpectedParticipant;
		return false;
	}
	if (!Receipt.IsValid())
	{
		OutFailureResult = ESeinBootstrapConsensusSubmitResult::InvalidReceipt;
		return false;
	}
	if (Receipt.ContractDigest != Context.MatchSettingsDigest)
	{
		OutFailureResult = FailTerminal(
			ESeinBootstrapConsensusFailure::ContractDigestMismatch,
			ESeinBootstrapConsensusSubmitResult::ContractDigestMismatch);
		return false;
	}
	if (Receipt.SimulationContentDigest
		!= Context.SimulationContentDigest)
	{
		OutFailureResult = FailTerminal(
			ESeinBootstrapConsensusFailure::
				SimulationContentDigestMismatch,
			ESeinBootstrapConsensusSubmitResult::
				SimulationContentDigestMismatch);
		return false;
	}
	return true;
}

ESeinBootstrapConsensusSubmitResult
FSeinBootstrapConsensus::SubmitPhaseEvidence(
	const FSeinProtocolContext& MessageContext,
	FSeinNetworkParticipantID AuthenticatedParticipant,
	const FSeinMatchBootstrapReceipt& Receipt,
	ESeinBootstrapConsensusState CollectingState,
	ESeinBootstrapConsensusState CompletedState,
	ESeinBootstrapConsensusSubmitResult CompletionResult,
	TMap<FSeinNetworkParticipantID, FSeinMatchBootstrapReceipt>& Evidence)
{
	if (State == ESeinBootstrapConsensusState::Failed)
	{
		return ESeinBootstrapConsensusSubmitResult::AlreadyFailed;
	}
	if (State == ESeinBootstrapConsensusState::Launched)
	{
		return ESeinBootstrapConsensusSubmitResult::InvalidPhase;
	}
	ESeinBootstrapConsensusSubmitResult ValidationResult;
	if (!ValidateSubmissionEnvelope(
		MessageContext, AuthenticatedParticipant, Receipt, ValidationResult))
	{
		return ValidationResult;
	}
	if (const FSeinMatchBootstrapReceipt* Existing =
		Evidence.Find(AuthenticatedParticipant))
	{
		if (*Existing == Receipt)
		{
			return ESeinBootstrapConsensusSubmitResult::IdenticalRetry;
		}
		return FailTerminal(
			ESeinBootstrapConsensusFailure::ConflictingRetry,
			ESeinBootstrapConsensusSubmitResult::ConflictingRetry);
	}
	if (!AgreedReceipt.IsSet() || AgreedReceipt.GetValue() != Receipt)
	{
		return FailTerminal(
			ESeinBootstrapConsensusFailure::PhaseReceiptMismatch,
			ESeinBootstrapConsensusSubmitResult::ReceiptDisagreement);
	}
	if (State != CollectingState)
	{
		return ESeinBootstrapConsensusSubmitResult::InvalidPhase;
	}

	Evidence.Add(AuthenticatedParticipant, Receipt);
	if (Evidence.Num() != RequiredParticipants.Num())
	{
		return ESeinBootstrapConsensusSubmitResult::Accepted;
	}
	for (const FSeinNetworkParticipantID Required : RequiredParticipants)
	{
		if (!Evidence.Contains(Required))
		{
			return ESeinBootstrapConsensusSubmitResult::Accepted;
		}
	}

	State = CompletedState;
	return CompletionResult;
}

void FSeinBootstrapConsensus::Reset()
{
	State = ESeinBootstrapConsensusState::Unconfigured;
	Failure = ESeinBootstrapConsensusFailure::None;
	Context = FSeinProtocolContext();
	RequiredParticipants.Reset();
	RequiredParticipantSet.Reset();
	SubmittedReceipts.Reset();
	AuthorizedReadyReceipts.Reset();
	CandidateReceipt.Reset();
	AgreedReceipt.Reset();
}

TArray<FSeinNetworkParticipantID>
FSeinBootstrapConsensus::GetMissingParticipants() const
{
	TArray<FSeinNetworkParticipantID> Missing;
	const TMap<FSeinNetworkParticipantID, FSeinMatchBootstrapReceipt>* Evidence = nullptr;
	if (State == ESeinBootstrapConsensusState::CollectingReceipts)
	{
		Evidence = &SubmittedReceipts;
	}
	else if (State == ESeinBootstrapConsensusState::CollectingAuthorizedReady)
	{
		Evidence = &AuthorizedReadyReceipts;
	}
	if (!Evidence)
	{
		return Missing;
	}
	Missing.Reserve(RequiredParticipants.Num() - Evidence->Num());
	for (const FSeinNetworkParticipantID Participant : RequiredParticipants)
	{
		if (!Evidence->Contains(Participant))
		{
			Missing.Add(Participant);
		}
	}
	return Missing;
}

bool FSeinBootstrapConsensus::GetAgreedReceipt(
	FSeinMatchBootstrapReceipt& OutReceipt) const
{
	OutReceipt = FSeinMatchBootstrapReceipt();
	if (!AgreedReceipt.IsSet())
	{
		return false;
	}
	OutReceipt = AgreedReceipt.GetValue();
	return true;
}

bool FSeinBootstrapConsensus::IsLaunchInFlight() const
{
	return State == ESeinBootstrapConsensusState::CollectingReceipts
		|| State == ESeinBootstrapConsensusState::ReceiptAgreed
		|| State == ESeinBootstrapConsensusState::CollectingAuthorizedReady
		|| State == ESeinBootstrapConsensusState::AuthorizedReady;
}

bool FSeinBootstrapConsensus::ParticipantLess(
	const FSeinNetworkParticipantID& A,
	const FSeinNetworkParticipantID& B)
{
	if (A.Value.A != B.Value.A) return A.Value.A < B.Value.A;
	if (A.Value.B != B.Value.B) return A.Value.B < B.Value.B;
	if (A.Value.C != B.Value.C) return A.Value.C < B.Value.C;
	return A.Value.D < B.Value.D;
}

ESeinBootstrapConsensusSubmitResult FSeinBootstrapConsensus::FailTerminal(
	ESeinBootstrapConsensusFailure InFailure,
	ESeinBootstrapConsensusSubmitResult Result)
{
	if (State == ESeinBootstrapConsensusState::Failed)
	{
		return ESeinBootstrapConsensusSubmitResult::AlreadyFailed;
	}
	Failure = InFailure;
	State = ESeinBootstrapConsensusState::Failed;
	return Result;
}
