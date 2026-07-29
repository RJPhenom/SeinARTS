/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file SeinTurnAggregator.cpp
 */

#include "SeinTurnAggregator.h"

ESeinTurnAggregatorConfigResult FSeinTurnAggregator::Configure(
	const FSeinProtocolContext& InContext,
	const TArray<FSeinParticipantBinding>& Bindings)
{
	if (!InContext.IsValid()) return ESeinTurnAggregatorConfigResult::InvalidContext;
	if (Bindings.Num() > SeinNetProtocolLimits::MaxParticipants)
	{
		return ESeinTurnAggregatorConfigResult::TooManyParticipants;
	}

	TSet<FSeinNetworkParticipantID> Participants;
	TSet<FSeinNetworkParticipantID> NewEligibleCoordinators;
	TSet<FSeinPlayerID> CommandSlots;
	TArray<FSeinTurnAuthor> NewExpectedAuthors;
	TSet<FSeinTurnAuthor> NewExpectedAuthorSet;
	for (const FSeinParticipantBinding& Binding : Bindings)
	{
		if (!Binding.IsValid()) return ESeinTurnAggregatorConfigResult::InvalidBinding;
		if (Participants.Contains(Binding.ParticipantID))
		{
			return ESeinTurnAggregatorConfigResult::DuplicateParticipant;
		}
		Participants.Add(Binding.ParticipantID);
		if (Binding.bCanCoordinate)
		{
			NewEligibleCoordinators.Add(Binding.ParticipantID);
		}

		for (const FSeinPlayerID Slot : Binding.CommandSlots)
		{
			if (CommandSlots.Contains(Slot))
			{
				return ESeinTurnAggregatorConfigResult::DuplicateCommandSlot;
			}
			CommandSlots.Add(Slot);
			NewExpectedAuthors.Emplace(Binding.ParticipantID, Slot);
			if (NewExpectedAuthors.Num()
				> SeinNetProtocolLimits::MaxCommandAuthors)
			{
				return ESeinTurnAggregatorConfigResult::TooManyCommandAuthors;
			}
		}
	}
	if (SeinComputeMembershipDigest(Bindings) != InContext.MembershipDigest)
	{
		return ESeinTurnAggregatorConfigResult::MembershipDigestMismatch;
	}
	if (!NewEligibleCoordinators.Contains(InContext.CoordinatorParticipantID))
	{
		return ESeinTurnAggregatorConfigResult::InvalidCoordinator;
	}

	NewExpectedAuthors.Sort([](const FSeinTurnAuthor& A, const FSeinTurnAuthor& B)
	{
		return FSeinTurnAuthor::CanonicalLess(A, B);
	});
	for (const FSeinTurnAuthor& Author : NewExpectedAuthors)
	{
		NewExpectedAuthorSet.Add(Author);
	}

	ExpectedAuthors = MoveTemp(NewExpectedAuthors);
	ExpectedAuthorSet = MoveTemp(NewExpectedAuthorSet);
	EligibleCoordinators = MoveTemp(NewEligibleCoordinators);
	PendingTurns.Reset();
	CommittedTurns.Reset();
	TurnRejectionFloor = INDEX_NONE;
	Context = InContext;
	bConfigured = true;
	return ESeinTurnAggregatorConfigResult::Configured;
}

ESeinCoordinatorTermAdvanceResult FSeinTurnAggregator::AdvanceCoordinatorTerm(
	const FSeinProtocolContext& NewContext)
{
	if (!bConfigured) return ESeinCoordinatorTermAdvanceResult::NotConfigured;
	if (!NewContext.IsValid()) return ESeinCoordinatorTermAdvanceResult::InvalidContext;
	if (NewContext.ProtocolVersion != Context.ProtocolVersion
		|| NewContext.MatchInstanceID != Context.MatchInstanceID
		|| NewContext.LockstepEpoch != Context.LockstepEpoch
		|| NewContext.MembershipRevision != Context.MembershipRevision
		|| NewContext.MembershipDigest != Context.MembershipDigest
		|| NewContext.DestinationWorldDigest != Context.DestinationWorldDigest
		|| NewContext.MatchSettingsDigest != Context.MatchSettingsDigest
		|| NewContext.SimulationContentDigest
			!= Context.SimulationContentDigest
		|| NewContext.CommandProtocolDigest != Context.CommandProtocolDigest)
	{
		return ESeinCoordinatorTermAdvanceResult::IncompatibleContext;
	}
	if (NewContext.CoordinatorTerm <= Context.CoordinatorTerm)
	{
		return ESeinCoordinatorTermAdvanceResult::TermNotNewer;
	}
	if (!EligibleCoordinators.Contains(NewContext.CoordinatorParticipantID))
	{
		return ESeinCoordinatorTermAdvanceResult::InvalidCoordinator;
	}

	Context = NewContext;
	return ESeinCoordinatorTermAdvanceResult::Advanced;
}

ESeinTurnSubmitResult FSeinTurnAggregator::Submit(
	const FSeinProtocolContext& MessageContext,
	int32 TurnID,
	const FSeinTurnAuthor& Author,
	const TArray<FSeinCommand>& Commands)
{
	return Submit(
		MessageContext, TurnID, Author, Commands,
		[](TConstArrayView<FSeinCommand>) { return true; });
}

ESeinTurnSubmitResult FSeinTurnAggregator::Submit(
	const FSeinProtocolContext& MessageContext,
	int32 TurnID,
	const FSeinTurnAuthor& Author,
	const TArray<FSeinCommand>& Commands,
	TFunctionRef<bool(TConstArrayView<FSeinCommand>)> CanAcceptProspectiveTurn)
{
	if (!bConfigured || MessageContext != Context)
	{
		return ESeinTurnSubmitResult::InvalidContext;
	}
	if (TurnID < 0) return ESeinTurnSubmitResult::InvalidTurn;
	if (IsTurnRetired(TurnID)) return ESeinTurnSubmitResult::TurnRetired;
	if (!IsExpectedAuthor(Author)) return ESeinTurnSubmitResult::UnexpectedAuthor;
	if (CommittedTurns.Contains(TurnID)) return ESeinTurnSubmitResult::TurnCommitted;

	const FPendingTurn* ExistingPending = PendingTurns.Find(TurnID);
	if (const TArray<FSeinCommand>* Existing =
		ExistingPending ? ExistingPending->Batches.Find(Author) : nullptr)
	{
		return AreBatchesIdentical(*Existing, Commands)
			? ESeinTurnSubmitResult::IdenticalRetry
			: ESeinTurnSubmitResult::ConflictingRetry;
	}

	TArray<FSeinCommand> ProspectiveTurn;
	for (const FSeinTurnAuthor& Expected : ExpectedAuthors)
	{
		if (Expected == Author)
		{
			ProspectiveTurn.Append(Commands);
			continue;
		}
		if (const TArray<FSeinCommand>* Existing =
			ExistingPending ? ExistingPending->Batches.Find(Expected) : nullptr)
		{
			ProspectiveTurn.Append(*Existing);
		}
	}
	if (!CanAcceptProspectiveTurn(ProspectiveTurn))
		return ESeinTurnSubmitResult::AggregateRejected;

	PendingTurns.FindOrAdd(TurnID).Batches.Add(Author, Commands);
	return ESeinTurnSubmitResult::Accepted;
}

ESeinTurnCommitResult FSeinTurnAggregator::TryCommit(
	const FSeinProtocolContext& MessageContext,
	int32 TurnID,
	TArray<FSeinCommand>& OutCommands)
{
	OutCommands.Reset();
	if (!bConfigured || MessageContext != Context)
	{
		return ESeinTurnCommitResult::InvalidContext;
	}
	if (TurnID < 0) return ESeinTurnCommitResult::InvalidTurn;
	if (IsTurnRetired(TurnID)) return ESeinTurnCommitResult::TurnRetired;
	if (CommittedTurns.Contains(TurnID)) return ESeinTurnCommitResult::AlreadyCommitted;

	const FPendingTurn* Pending = PendingTurns.Find(TurnID);
	if (!ExpectedAuthors.IsEmpty())
	{
		if (!Pending || Pending->Batches.Num() < ExpectedAuthors.Num())
		{
			return ESeinTurnCommitResult::NotReady;
		}
		for (const FSeinTurnAuthor& Author : ExpectedAuthors)
		{
			if (!Pending->Batches.Contains(Author))
			{
				return ESeinTurnCommitResult::NotReady;
			}
		}

		for (const FSeinTurnAuthor& Author : ExpectedAuthors)
		{
			OutCommands.Append(Pending->Batches.FindChecked(Author));
		}
	}

	PendingTurns.Remove(TurnID);
	CommittedTurns.Add(TurnID);
	return ESeinTurnCommitResult::Committed;
}

bool FSeinTurnAggregator::HasSubmission(
	int32 TurnID,
	const FSeinTurnAuthor& Author) const
{
	if (!IsExpectedAuthor(Author) || IsTurnRetired(TurnID)) return false;
	if (CommittedTurns.Contains(TurnID)) return true;
	const FPendingTurn* Pending = PendingTurns.Find(TurnID);
	return Pending && Pending->Batches.Contains(Author);
}

int32 FSeinTurnAggregator::GetSubmittedAuthorCount(int32 TurnID) const
{
	if (IsTurnRetired(TurnID)) return 0;
	if (CommittedTurns.Contains(TurnID)) return ExpectedAuthors.Num();
	const FPendingTurn* Pending = PendingTurns.Find(TurnID);
	return Pending ? Pending->Batches.Num() : 0;
}

TArray<FSeinTurnAuthor> FSeinTurnAggregator::GetMissingAuthors(int32 TurnID) const
{
	TArray<FSeinTurnAuthor> Missing;
	if (!bConfigured || TurnID < 0 || IsTurnRetired(TurnID)
		|| CommittedTurns.Contains(TurnID))
	{
		return Missing;
	}

	const FPendingTurn* Pending = PendingTurns.Find(TurnID);
	for (const FSeinTurnAuthor& Author : ExpectedAuthors)
	{
		if (!Pending || !Pending->Batches.Contains(Author)) Missing.Add(Author);
	}
	return Missing;
}

TArray<int32> FSeinTurnAggregator::GetPendingTurnIDs() const
{
	TArray<int32> Turns;
	PendingTurns.GetKeys(Turns);
	Turns.Sort();
	return Turns;
}

void FSeinTurnAggregator::PruneThroughTurn(int32 InclusiveTurn)
{
	if (InclusiveTurn <= TurnRejectionFloor) return;
	TurnRejectionFloor = InclusiveTurn;

	for (auto It = PendingTurns.CreateIterator(); It; ++It)
	{
		if (It.Key() <= TurnRejectionFloor) It.RemoveCurrent();
	}
	for (auto It = CommittedTurns.CreateIterator(); It; ++It)
	{
		if (*It <= TurnRejectionFloor) It.RemoveCurrent();
	}
}

void FSeinTurnAggregator::Reset()
{
	bConfigured = false;
	Context = FSeinProtocolContext();
	ExpectedAuthors.Reset();
	ExpectedAuthorSet.Reset();
	EligibleCoordinators.Reset();
	PendingTurns.Reset();
	CommittedTurns.Reset();
	TurnRejectionFloor = INDEX_NONE;
}

bool FSeinTurnAggregator::IsExpectedAuthor(const FSeinTurnAuthor& Author) const
{
	return Author.IsValid() && ExpectedAuthorSet.Contains(Author);
}

bool FSeinTurnAggregator::AreBatchesIdentical(
	const TArray<FSeinCommand>& A,
	const TArray<FSeinCommand>& B)
{
	if (A.Num() != B.Num()) return false;
	const UScriptStruct* CommandStruct = FSeinCommand::StaticStruct();
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (!CommandStruct->CompareScriptStruct(&A[Index], &B[Index], 0)) return false;
	}
	return true;
}
