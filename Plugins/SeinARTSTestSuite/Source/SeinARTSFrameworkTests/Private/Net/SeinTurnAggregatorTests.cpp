#include "CQTest.h"
#include "SeinNetProtocolTypes.h"
#include "SeinTurnAggregator.h"

#include <initializer_list>

namespace UE::SeinARTSTests
{
	namespace
	{
		FSeinMatchInstanceID MakeMatch()
		{
			return FSeinMatchInstanceID(FGuid(
				0x11111111, 0x22222222, 0x33333333, 0x44444444));
		}

		FSeinNetworkParticipantID MakeParticipant(uint32 LastWord)
		{
			return FSeinNetworkParticipantID(FGuid(
				0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, LastWord));
		}

		FSeinProtocolContext MakeContext(
			int64 Epoch = 1,
			int64 Term = 1,
			int64 MembershipRevision = 1,
			FGuid MembershipDigest = FGuid(
				0x55555555, 0x66666666, 0x77777777, 0x88888888),
			FSeinNetworkParticipantID Coordinator = MakeParticipant(1),
			FGuid DestinationWorldDigest = FGuid(
				0x09090909, 0x18181818, 0x27272727, 0x36363636),
			FGuid MatchSettingsDigest = FGuid(
				0x10101010, 0x20202020, 0x30303030, 0x40404040),
			FGuid SimulationContentDigest = FGuid(
				0x41414141, 0x42424242, 0x43434343, 0x44444444),
			FGuid CommandProtocolDigest = FGuid(
				0x51515151, 0x62626262, 0x73737373, 0x84848484))
		{
			return FSeinProtocolContext(
				MakeMatch(), Epoch, Coordinator, Term, MembershipRevision,
				MembershipDigest, DestinationWorldDigest, MatchSettingsDigest,
				SimulationContentDigest,
				CommandProtocolDigest);
		}

		FSeinParticipantBinding MakeBinding(
			FSeinNetworkParticipantID Participant,
			std::initializer_list<uint8> Slots)
		{
			FSeinParticipantBinding Binding;
			Binding.ParticipantID = Participant;
			for (const uint8 Slot : Slots) Binding.CommandSlots.Emplace(Slot);
			Binding.bSimulates = true;
			Binding.bReportsWorldRoots = true;
			Binding.bCanCoordinate = true;
			return Binding;
		}

		FSeinProtocolContext MakeContextFor(
			const TArray<FSeinParticipantBinding>& Bindings,
			int64 Epoch = 1,
			int64 Term = 1,
			int64 MembershipRevision = 1)
		{
			return MakeContext(
				Epoch,
				Term,
				MembershipRevision,
				SeinComputeMembershipDigest(Bindings),
				Bindings.IsEmpty()
					? FSeinNetworkParticipantID::Invalid()
					: Bindings[0].ParticipantID);
		}

		int32 AsInt(ESeinTurnAggregatorConfigResult Value) { return static_cast<int32>(Value); }
		int32 AsInt(ESeinCoordinatorTermAdvanceResult Value) { return static_cast<int32>(Value); }
		int32 AsInt(ESeinTurnSubmitResult Value) { return static_cast<int32>(Value); }
		int32 AsInt(ESeinTurnCommitResult Value) { return static_cast<int32>(Value); }
	}

	TEST(ProtocolContextHasCanonicalIdentityAndStrictValidity, "SeinARTS.Unit.Network.Protocol")
	{
		ASSERT_THAT(AreEqual(
			11, FSeinProtocolContext::CurrentProtocolVersion));
		const FSeinProtocolContext Context = MakeContext(7, 3);
		ASSERT_THAT(IsTrue(Context.IsValid()));
		ASSERT_THAT(AreEqual(
			FString(TEXT(
				"protocol=11 match=11111111-2222-2222-3333-333344444444 "
				"epoch=7 coordinator=aaaaaaaa-bbbb-bbbb-cccc-cccc00000001/3 "
				"membership=1/55555555-6666-6666-7777-777788888888 "
				"destination=09090909-1818-1818-2727-272736363636 "
				"settings=10101010-2020-2020-3030-303040404040 "
				"content=41414141-4242-4242-4343-434344444444 "
				"commands=51515151-6262-6262-7373-737384848484")),
			Context.ToCanonicalDebugString()));
		const FSeinProtocolContext EqualContext = Context;
		ASSERT_THAT(IsTrue(Context == EqualContext));
		ASSERT_THAT(AreEqual(GetTypeHash(Context), GetTypeHash(EqualContext)));

		FSeinProtocolContext Invalid = Context;
		Invalid.LockstepEpoch = 0;
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.CoordinatorParticipantID = FSeinNetworkParticipantID::Invalid();
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.CoordinatorTerm = 0;
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.MembershipRevision = 0;
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.MembershipDigest = FGuid();
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.MatchSettingsDigest = FGuid();
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.SimulationContentDigest = FGuid();
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.CommandProtocolDigest = FGuid();
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		Invalid = Context;
		Invalid.ProtocolVersion = FSeinProtocolContext::CurrentProtocolVersion + 1;
		ASSERT_THAT(IsFalse(Invalid.IsValid()));
		ASSERT_THAT(IsTrue(ESeinMatchTravelIntent::NewMatch != ESeinMatchTravelIntent::ContinueMatch));
	}

	TEST(ParticipantCapabilitiesDoNotImplyGameplaySlotOwnership, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinParticipantBinding Dedicated;
		Dedicated.ParticipantID = MakeParticipant(1);
		Dedicated.bSimulates = true;
		Dedicated.bReportsWorldRoots = true;
		Dedicated.bCanCoordinate = true;
		Dedicated.bCanAdministerMatch = true;
		ASSERT_THAT(IsTrue(Dedicated.IsValid()));
		ASSERT_THAT(AreEqual(0, Dedicated.CommandSlots.Num()));

		Dedicated.bSimulates = false;
		ASSERT_THAT(IsFalse(Dedicated.IsValid()));
		Dedicated.bReportsWorldRoots = false;
		ASSERT_THAT(IsTrue(Dedicated.IsValid()));
		Dedicated.CommandSlots = {FSeinPlayerID(2), FSeinPlayerID(2)};
		ASSERT_THAT(IsFalse(Dedicated.IsValid()));
	}

	TEST(MembershipDigestIsCanonicalAndCoversEveryBindingField, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinParticipantBinding A = MakeBinding(MakeParticipant(1), {2, 1});
		A.bReportsWorldRoots = false;
		A.bCanCoordinate = true;
		FSeinParticipantBinding B = MakeBinding(MakeParticipant(2), {3});
		B.bReportsWorldRoots = false;
		B.bCanAdministerMatch = true;
		const TArray<FSeinParticipantBinding> Bindings{A, B};
		const FGuid Digest = SeinComputeMembershipDigest(Bindings);
		ASSERT_THAT(IsTrue(Digest.IsValid()));

		A.CommandSlots = {FSeinPlayerID(1), FSeinPlayerID(2)};
		ASSERT_THAT(IsTrue(Digest == SeinComputeMembershipDigest({B, A})));

		TArray<FSeinParticipantBinding> Changed = Bindings;
		Changed[0].CommandSlots[0] = FSeinPlayerID(4);
		ASSERT_THAT(IsTrue(Digest != SeinComputeMembershipDigest(Changed)));
		Changed = Bindings;
		Changed[0].ParticipantID = MakeParticipant(9);
		ASSERT_THAT(IsTrue(Digest != SeinComputeMembershipDigest(Changed)));

		for (int32 Capability = 0; Capability < 4; ++Capability)
		{
			Changed = Bindings;
			switch (Capability)
			{
			case 0: Changed[0].bSimulates = !Changed[0].bSimulates; break;
			case 1:
				Changed[0].bReportsWorldRoots =
					!Changed[0].bReportsWorldRoots;
				break;
			case 2: Changed[0].bCanCoordinate = !Changed[0].bCanCoordinate; break;
			default: Changed[0].bCanAdministerMatch = !Changed[0].bCanAdministerMatch; break;
			}
			ASSERT_THAT(IsTrue(Digest != SeinComputeMembershipDigest(Changed)));
		}
	}

	TEST(AggregatorRejectsAmbiguousExpectedAuthors, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinTurnAggregator Aggregator;
		const FSeinParticipantBinding A = MakeBinding(MakeParticipant(1), {1});
		FSeinParticipantBinding DuplicateParticipant = MakeBinding(MakeParticipant(1), {2});
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::DuplicateParticipant),
			AsInt(Aggregator.Configure(MakeContext(), {A, DuplicateParticipant}))));
		ASSERT_THAT(IsFalse(Aggregator.IsConfigured()));
		ASSERT_THAT(AreEqual(0, Aggregator.GetExpectedAuthors().Num()));

		const FSeinParticipantBinding DuplicateSlot = MakeBinding(MakeParticipant(2), {1});
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::DuplicateCommandSlot),
			AsInt(Aggregator.Configure(MakeContext(), {A, DuplicateSlot}))));
		ASSERT_THAT(IsFalse(Aggregator.IsConfigured()));
		ASSERT_THAT(AreEqual(0, Aggregator.GetExpectedAuthors().Num()));

		FSeinProtocolContext WrongDigest = MakeContextFor({A});
		WrongDigest.MembershipDigest = FGuid(1, 2, 3, 4);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::MembershipDigestMismatch),
			AsInt(Aggregator.Configure(WrongDigest, {A}))));
		ASSERT_THAT(IsFalse(Aggregator.IsConfigured()));
		ASSERT_THAT(AreEqual(0, Aggregator.GetExpectedAuthors().Num()));

		FSeinProtocolContext InvalidCoordinator = MakeContextFor({A});
		InvalidCoordinator.CoordinatorParticipantID = MakeParticipant(99);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::InvalidCoordinator),
			AsInt(Aggregator.Configure(InvalidCoordinator, {A}))));

		FSeinParticipantBinding TooMany = MakeBinding(MakeParticipant(3), {});
		for (int32 Slot = 1;
			Slot <= SeinNetProtocolLimits::MaxCommandAuthors + 1; ++Slot)
		{
			TooMany.CommandSlots.Emplace(static_cast<uint8>(Slot));
		}
		const TArray<FSeinParticipantBinding> TooManyBindings{TooMany};
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::TooManyCommandAuthors),
			AsInt(Aggregator.Configure(
				MakeContextFor(TooManyBindings), TooManyBindings))));

		TArray<FSeinParticipantBinding> TooManyParticipants;
		for (int32 Index = 0;
			Index <= SeinNetProtocolLimits::MaxParticipants;
			++Index)
		{
			TooManyParticipants.Add(MakeBinding(
				MakeParticipant(Index + 1), {}));
		}
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::TooManyParticipants),
			AsInt(Aggregator.Configure(
				MakeContextFor(TooManyParticipants), TooManyParticipants))));
	}

	TEST(AggregatorCommitsCompleteTurnInCanonicalSlotOrder, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinTurnAggregator Aggregator;
		const FSeinNetworkParticipantID ParticipantA = MakeParticipant(1);
		const FSeinNetworkParticipantID ParticipantB = MakeParticipant(2);
		const FSeinParticipantBinding SlotTwo = MakeBinding(ParticipantA, {2});
		const FSeinParticipantBinding SlotOne = MakeBinding(ParticipantB, {1});
		const FSeinProtocolContext Context = MakeContextFor({SlotTwo, SlotOne});
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::Configured),
			AsInt(Aggregator.Configure(Context, {SlotTwo, SlotOne}))));

		const FSeinTurnAuthor AuthorOne(ParticipantB, FSeinPlayerID(1));
		const FSeinTurnAuthor AuthorTwo(ParticipantA, FSeinPlayerID(2));
		FSeinCommand One;
		One.QueueIndex = 101;
		FSeinCommand Two;
		Two.QueueIndex = 202;

		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::InvalidContext),
			AsInt(Aggregator.Submit(MakeContext(2), 5, AuthorOne, {One}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::UnexpectedAuthor),
			AsInt(Aggregator.Submit(
				Context,
				5,
				FSeinTurnAuthor(MakeParticipant(9), FSeinPlayerID(1)),
				{One}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(Context, 5, AuthorTwo, {Two}))));
		ASSERT_THAT(IsTrue(Aggregator.HasSubmission(5, AuthorTwo)));
		ASSERT_THAT(IsFalse(Aggregator.HasSubmission(5, AuthorOne)));
		ASSERT_THAT(AreEqual(1, Aggregator.GetSubmittedAuthorCount(5)));
		const TArray<FSeinTurnAuthor> Missing = Aggregator.GetMissingAuthors(5);
		ASSERT_THAT(AreEqual(1, Missing.Num()));
		ASSERT_THAT(IsTrue(Missing[0] == AuthorOne));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::IdenticalRetry),
			AsInt(Aggregator.Submit(Context, 5, AuthorTwo, {Two}))));

		FSeinCommand Conflict = Two;
		Conflict.QueueIndex = 999;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::ConflictingRetry),
			AsInt(Aggregator.Submit(Context, 5, AuthorTwo, {Conflict}))));

		TArray<FSeinCommand> Assembled;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::NotReady),
			AsInt(Aggregator.TryCommit(Context, 5, Assembled))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(Context, 5, AuthorOne, {One}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::Committed),
			AsInt(Aggregator.TryCommit(Context, 5, Assembled))));
		ASSERT_THAT(AreEqual(2, Assembled.Num()));
		ASSERT_THAT(AreEqual(101, Assembled[0].QueueIndex));
		ASSERT_THAT(AreEqual(202, Assembled[1].QueueIndex));
		ASSERT_THAT(AreEqual(2, Aggregator.GetSubmittedAuthorCount(5)));
		ASSERT_THAT(AreEqual(0, Aggregator.GetMissingAuthors(5).Num()));

		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::TurnCommitted),
			AsInt(Aggregator.Submit(Context, 5, AuthorOne, {One}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::AlreadyCommitted),
			AsInt(Aggregator.TryCommit(Context, 5, Assembled))));
		ASSERT_THAT(AreEqual(0, Assembled.Num()));
	}

	TEST(AggregatorSupportsARefereeOnlyTurnWithoutInventingAPlayerSlot, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinTurnAggregator Aggregator;
		FSeinParticipantBinding Referee;
		Referee.ParticipantID = MakeParticipant(7);
		Referee.bSimulates = true;
		Referee.bReportsWorldRoots = true;
		Referee.bCanCoordinate = true;
		const FSeinProtocolContext Context = MakeContextFor({Referee});
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::Configured),
			AsInt(Aggregator.Configure(Context, {Referee}))));

		TArray<FSeinCommand> Commands;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::Committed),
			AsInt(Aggregator.TryCommit(Context, 0, Commands))));
		ASSERT_THAT(AreEqual(0, Commands.Num()));
	}

	TEST(AggregatorPruningBoundsStorageWithoutReopeningOldTurns, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinTurnAggregator Aggregator;
		const FSeinNetworkParticipantID Participant = MakeParticipant(4);
		const FSeinTurnAuthor Author(Participant, FSeinPlayerID(1));
		const TArray<FSeinParticipantBinding> Bindings{MakeBinding(Participant, {1})};
		const FSeinProtocolContext Context = MakeContextFor(Bindings);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::Configured),
			AsInt(Aggregator.Configure(Context, Bindings))));

		FSeinCommand Command;
		Command.QueueIndex = 42;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(Context, 2, Author, {Command}))));
		Aggregator.PruneThroughTurn(2);
		ASSERT_THAT(AreEqual(2, Aggregator.GetTurnRejectionFloor()));
		ASSERT_THAT(IsTrue(Aggregator.IsTurnRetired(2)));
		ASSERT_THAT(IsFalse(Aggregator.HasSubmission(2, Author)));
		ASSERT_THAT(AreEqual(0, Aggregator.GetSubmittedAuthorCount(2)));
		ASSERT_THAT(AreEqual(0, Aggregator.GetMissingAuthors(2).Num()));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::TurnRetired),
			AsInt(Aggregator.Submit(Context, 2, Author, {Command}))));

		TArray<FSeinCommand> Commands;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::TurnRetired),
			AsInt(Aggregator.TryCommit(Context, 2, Commands))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(Context, 3, Author, {Command}))));
	}

	TEST(CoordinatorTermAdvancePreservesAndProtectsTheLedger, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinTurnAggregator Aggregator;
		const FSeinNetworkParticipantID ParticipantA = MakeParticipant(1);
		const FSeinNetworkParticipantID ParticipantB = MakeParticipant(2);
		const TArray<FSeinParticipantBinding> Bindings{
			MakeBinding(ParticipantA, {1}),
			MakeBinding(ParticipantB, {2})};
		const FSeinProtocolContext TermOne = MakeContextFor(Bindings, 7, 1, 4);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::Configured),
			AsInt(Aggregator.Configure(TermOne, Bindings))));

		const FSeinTurnAuthor AuthorA(ParticipantA, FSeinPlayerID(1));
		const FSeinTurnAuthor AuthorB(ParticipantB, FSeinPlayerID(2));
		FSeinCommand A;
		A.QueueIndex = 1;
		FSeinCommand B;
		B.QueueIndex = 2;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(TermOne, 5, AuthorA, {A}))));
		Aggregator.PruneThroughTurn(1);

		FSeinProtocolContext Incompatible = TermOne;
		Incompatible.CoordinatorTerm = 2;
		Incompatible.LockstepEpoch = 8;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::IncompatibleContext),
			AsInt(Aggregator.AdvanceCoordinatorTerm(Incompatible))));
		Incompatible = TermOne;
		Incompatible.CoordinatorTerm = 2;
		Incompatible.MembershipRevision = 5;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::IncompatibleContext),
			AsInt(Aggregator.AdvanceCoordinatorTerm(Incompatible))));
		Incompatible = TermOne;
		Incompatible.CoordinatorTerm = 2;
		Incompatible.MembershipDigest = FGuid(1, 2, 3, 4);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::IncompatibleContext),
			AsInt(Aggregator.AdvanceCoordinatorTerm(Incompatible))));
		Incompatible = TermOne;
		Incompatible.CoordinatorTerm = 2;
		Incompatible.MatchSettingsDigest = FGuid(4, 3, 2, 1);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::IncompatibleContext),
			AsInt(Aggregator.AdvanceCoordinatorTerm(Incompatible))));
		Incompatible = TermOne;
		Incompatible.CoordinatorTerm = 2;
		Incompatible.SimulationContentDigest = FGuid(8, 7, 6, 5);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::IncompatibleContext),
			AsInt(Aggregator.AdvanceCoordinatorTerm(Incompatible))));
		Incompatible = TermOne;
		Incompatible.CoordinatorTerm = 2;
		Incompatible.CommandProtocolDigest = FGuid(5, 6, 7, 8);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::IncompatibleContext),
			AsInt(Aggregator.AdvanceCoordinatorTerm(Incompatible))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::TermNotNewer),
			AsInt(Aggregator.AdvanceCoordinatorTerm(TermOne))));

		FSeinProtocolContext TermTwo = TermOne;
		TermTwo.CoordinatorTerm = 2;
		TermTwo.CoordinatorParticipantID = ParticipantB;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::Advanced),
			AsInt(Aggregator.AdvanceCoordinatorTerm(TermTwo))));
		ASSERT_THAT(AreEqual(1, Aggregator.GetTurnRejectionFloor()));
		ASSERT_THAT(AreEqual(2, Aggregator.GetExpectedAuthors().Num()));
		ASSERT_THAT(IsTrue(Aggregator.HasSubmission(5, AuthorA)));

		FSeinProtocolContext UnknownCoordinator = TermTwo;
		UnknownCoordinator.CoordinatorTerm = 3;
		UnknownCoordinator.CoordinatorParticipantID = MakeParticipant(99);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::InvalidCoordinator),
			AsInt(Aggregator.AdvanceCoordinatorTerm(UnknownCoordinator))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::InvalidContext),
			AsInt(Aggregator.Submit(TermOne, 5, AuthorB, {B}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(TermTwo, 5, AuthorB, {B}))));

		TArray<FSeinCommand> Commands;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::Committed),
			AsInt(Aggregator.TryCommit(TermTwo, 5, Commands))));
		FSeinProtocolContext TermThree = TermTwo;
		TermThree.CoordinatorTerm = 3;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinCoordinatorTermAdvanceResult::Advanced),
			AsInt(Aggregator.AdvanceCoordinatorTerm(TermThree))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::TurnCommitted),
			AsInt(Aggregator.Submit(TermThree, 5, AuthorA, {A}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::AlreadyCommitted),
			AsInt(Aggregator.TryCommit(TermThree, 5, Commands))));
	}

	TEST(FailedReconfigurationPreservesTheActiveLedger, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinTurnAggregator Aggregator;
		const FSeinNetworkParticipantID Participant = MakeParticipant(1);
		const FSeinParticipantBinding Binding = MakeBinding(Participant, {1});
		const FSeinProtocolContext Context = MakeContextFor({Binding});
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::Configured),
			AsInt(Aggregator.Configure(Context, {Binding}))));

		const FSeinTurnAuthor Author(Participant, FSeinPlayerID(1));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(Context, 7, Author, {}))));
		Aggregator.PruneThroughTurn(3);

		FSeinProtocolContext Invalid = Context;
		Invalid.MembershipDigest = FGuid();
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::InvalidContext),
			AsInt(Aggregator.Configure(Invalid, {Binding}))));
		ASSERT_THAT(IsTrue(Aggregator.IsConfigured()));
		ASSERT_THAT(IsTrue(Aggregator.GetContext() == Context));
		ASSERT_THAT(IsTrue(Aggregator.HasSubmission(7, Author)));
		ASSERT_THAT(AreEqual(3, Aggregator.GetTurnRejectionFloor()));
		ASSERT_THAT(AreEqual(1, Aggregator.GetPendingTurnIDs().Num()));
	}

	TEST(ProspectiveAdmissionRejectsBeforeMutationAndAllowsSmallerRetry, "SeinARTS.Unit.Network.Protocol")
	{
		FSeinTurnAggregator Aggregator;
		const FSeinNetworkParticipantID ParticipantA = MakeParticipant(1);
		const FSeinNetworkParticipantID ParticipantB = MakeParticipant(2);
		const TArray<FSeinParticipantBinding> Bindings{
			MakeBinding(ParticipantA, {1}), MakeBinding(ParticipantB, {2}) };
		const FSeinProtocolContext Context = MakeContextFor(Bindings);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnAggregatorConfigResult::Configured),
			AsInt(Aggregator.Configure(Context, Bindings))));

		FSeinCommand CommandA;
		CommandA.QueueIndex = 11;
		FSeinCommand CommandB;
		CommandB.QueueIndex = 22;
		const FSeinTurnAuthor AuthorA(ParticipantA, FSeinPlayerID(1));
		const FSeinTurnAuthor AuthorB(ParticipantB, FSeinPlayerID(2));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(
				Context, 9, AuthorB, {CommandB},
				[](TConstArrayView<FSeinCommand> Prospective)
				{
					return Prospective.Num() == 1 && Prospective[0].QueueIndex == 22;
				}))));

		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::AggregateRejected),
			AsInt(Aggregator.Submit(
				Context, 9, AuthorA, {CommandA},
				[](TConstArrayView<FSeinCommand> Prospective)
				{
					return !(Prospective.Num() == 2
						&& Prospective[0].QueueIndex == 11
						&& Prospective[1].QueueIndex == 22);
				}))));
		ASSERT_THAT(IsFalse(Aggregator.HasSubmission(9, AuthorA)));
		ASSERT_THAT(IsTrue(Aggregator.HasSubmission(9, AuthorB)));

		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnSubmitResult::Accepted),
			AsInt(Aggregator.Submit(
				Context, 9, AuthorA, {},
				[](TConstArrayView<FSeinCommand> Prospective)
				{
					return Prospective.Num() == 1 && Prospective[0].QueueIndex == 22;
				}))));
		TArray<FSeinCommand> Assembled;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinTurnCommitResult::Committed),
			AsInt(Aggregator.TryCommit(Context, 9, Assembled))));
		ASSERT_THAT(AreEqual(1, Assembled.Num()));
		ASSERT_THAT(AreEqual(22, Assembled[0].QueueIndex));
	}
}
