#include "CQTest.h"
#include "SeinBootstrapConsensus.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		const FGuid BootstrapMembershipDigest(
			0x70000000, 0x71000000, 0x72000000, 1);
		const FGuid BootstrapDestinationDigest(
			0x73000000, 0x74000000, 0x75000000, 1);
		const FGuid BootstrapSettingsDigest(
			0x76000000, 0x77000000, 0x78000000, 1);
		const FGuid BootstrapSimulationContentDigest(
			0x7c000000, 0x7d000000, 0x7e000000, 1);
		const FGuid BootstrapCommandDigest(
			0x79000000, 0x7a000000, 0x7b000000, 1);

		FSeinNetworkParticipantID BootstrapParticipant(uint32 Suffix)
		{
			return FSeinNetworkParticipantID(
				FGuid(0x10000000, 0x20000000, 0x30000000, Suffix));
		}

		FSeinProtocolContext BootstrapContext()
		{
			return FSeinProtocolContext(
				FSeinMatchInstanceID(
					FGuid(0x40000000, 0x50000000, 0x60000000, 1)),
				1,
				BootstrapParticipant(1),
				1,
				1,
				BootstrapMembershipDigest,
				BootstrapDestinationDigest,
				BootstrapSettingsDigest,
				BootstrapSimulationContentDigest,
				BootstrapCommandDigest);
		}

		FSeinMatchBootstrapReceipt BootstrapReceipt(uint32 Suffix = 1)
		{
			FSeinMatchBootstrapReceipt Receipt;
			Receipt.ContractDigest = BootstrapSettingsDigest;
			Receipt.SimulationContentDigest =
				BootstrapSimulationContentDigest;
			Receipt.StateContractDigest =
				FGuid(0x7f000000, 0x80000000, 0x81000000, 1);
			Receipt.PlanDigest =
				FGuid(0x82000000, 0x83000000, 0x84000000, Suffix);
			Receipt.InitialStateDigest =
				FGuid(0x85000000, 0x86000000, 0x87000000, Suffix);
			return Receipt;
		}

		int32 AsInt(ESeinBootstrapConsensusConfigResult Value)
		{
			return static_cast<int32>(Value);
		}

		int32 AsInt(ESeinBootstrapConsensusSubmitResult Value)
		{
			return static_cast<int32>(Value);
		}
	}

	TEST(BootstrapConsensusRequiresAValidBoundedSimulatingRoster,
		"SeinARTS.Unit.Network.Bootstrap")
	{
		FSeinBootstrapConsensus Consensus;
		FSeinProtocolContext InvalidContext = BootstrapContext();
		InvalidContext.LockstepEpoch = 0;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::InvalidContext),
			AsInt(Consensus.Configure(
				InvalidContext, {BootstrapParticipant(1)}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::NoRequiredParticipants),
			AsInt(Consensus.Configure(BootstrapContext(), {}))));

		TArray<FSeinNetworkParticipantID> OversizedRoster;
		for (int32 Index = 0;
			Index <= SeinNetProtocolLimits::MaxParticipants;
			++Index)
		{
			OversizedRoster.Add(BootstrapParticipant(Index + 1));
		}
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::TooManyRequiredParticipants),
			AsInt(Consensus.Configure(BootstrapContext(), OversizedRoster))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::DuplicateParticipant),
			AsInt(Consensus.Configure(
				BootstrapContext(),
				{BootstrapParticipant(1), BootstrapParticipant(1)}))));
		ASSERT_THAT(IsFalse(Consensus.IsConfigured()));
	}

	TEST(BootstrapAuthorizationIdentityCoversContextAndSeed,
		"SeinARTS.Unit.Network.Bootstrap")
	{
		const FSeinProtocolContext Context = BootstrapContext();
		const FGuid Baseline =
			SeinComputeBootstrapAuthorizationContextDigest(Context, 1234);
		ASSERT_THAT(IsTrue(Baseline.IsValid()));
		ASSERT_THAT(IsTrue(Baseline
			== SeinComputeBootstrapAuthorizationContextDigest(Context, 1234)));
		ASSERT_THAT(IsTrue(Baseline
			!= SeinComputeBootstrapAuthorizationContextDigest(Context, 1235)));

		FSeinProtocolContext NextTerm = Context;
		NextTerm.CoordinatorTerm++;
		ASSERT_THAT(IsTrue(Baseline
			!= SeinComputeBootstrapAuthorizationContextDigest(NextTerm, 1234)));

		FSeinProtocolContext OtherDestination = Context;
		OtherDestination.DestinationWorldDigest =
			FGuid(0x73000000, 0x74000000, 0x75000000, 2);
		ASSERT_THAT(IsTrue(Baseline
			!= SeinComputeBootstrapAuthorizationContextDigest(
				OtherDestination, 1234)));

		FSeinProtocolContext OtherSimulationContent = Context;
		OtherSimulationContent.SimulationContentDigest =
			FGuid(0x7c000000, 0x7d000000, 0x7e000000, 2);
		ASSERT_THAT(IsTrue(Baseline
			!= SeinComputeBootstrapAuthorizationContextDigest(
				OtherSimulationContent, 1234)));
	}

	TEST(BootstrapConsensusAgreesOnlyAfterEverySimPeerMatches,
		"SeinARTS.Unit.Network.Bootstrap")
	{
		FSeinBootstrapConsensus Consensus;
		const FSeinProtocolContext Context = BootstrapContext();
		const FSeinNetworkParticipantID A = BootstrapParticipant(1);
		const FSeinNetworkParticipantID B = BootstrapParticipant(2);
		const FSeinMatchBootstrapReceipt Receipt = BootstrapReceipt();
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(Consensus.Configure(Context, {B, A}))));
		ASSERT_THAT(IsTrue(Consensus.GetRequiredParticipants()[0] == A));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::Accepted),
			AsInt(Consensus.Submit(Context, A, Receipt))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::IdenticalRetry),
			AsInt(Consensus.Submit(Context, A, Receipt))));
		ASSERT_THAT(AreEqual(1, Consensus.GetSubmittedParticipantCount()));
		ASSERT_THAT(IsTrue(Consensus.GetMissingParticipants()[0] == B));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::AgreementReached),
			AsInt(Consensus.Submit(Context, B, Receipt))));
		ASSERT_THAT(AreEqual(2, Consensus.GetSubmittedParticipantCount()));

		FSeinMatchBootstrapReceipt Agreed;
		ASSERT_THAT(IsTrue(Consensus.GetAgreedReceipt(Agreed)));
		ASSERT_THAT(IsTrue(Agreed == Receipt));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::IdenticalRetry),
			AsInt(Consensus.Submit(Context, B, Receipt))));

		ASSERT_THAT(IsTrue(Consensus.BeginAuthorization()));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::Accepted),
			AsInt(Consensus.SubmitAuthorizedReady(Context, A, Receipt))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::IdenticalRetry),
			AsInt(Consensus.SubmitAuthorizedReady(Context, A, Receipt))));
		ASSERT_THAT(IsFalse(Consensus.BeginLaunch()));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::AuthorizationReady),
			AsInt(Consensus.SubmitAuthorizedReady(Context, B, Receipt))));
		ASSERT_THAT(AreEqual(2,
			Consensus.GetAuthorizedReadyParticipantCount()));

		ASSERT_THAT(IsTrue(Consensus.BeginLaunch()));
		ASSERT_THAT(IsTrue(
			Consensus.GetState() == ESeinBootstrapConsensusState::Launched));
		ASSERT_THAT(IsTrue(Consensus.IsLaunchComplete()));
		ASSERT_THAT(IsFalse(Consensus.IsLaunchInFlight()));

		// Launch is the terminal commit. Hostile or stale evidence cannot mutate
		// the committed state or retained prepare evidence.
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::InvalidPhase),
			AsInt(Consensus.Submit(Context, A, BootstrapReceipt(2)))));
		FSeinProtocolContext WrongContext = Context;
		WrongContext.CoordinatorTerm++;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::InvalidPhase),
			AsInt(Consensus.SubmitAuthorizedReady(
				WrongContext, B, BootstrapReceipt(2)))));
		ASSERT_THAT(IsTrue(
			Consensus.GetState() == ESeinBootstrapConsensusState::Launched));
		ASSERT_THAT(IsTrue(
			Consensus.GetFailure() == ESeinBootstrapConsensusFailure::None));
		ASSERT_THAT(AreEqual(2, Consensus.GetSubmittedParticipantCount()));
		ASSERT_THAT(AreEqual(2,
			Consensus.GetAuthorizedReadyParticipantCount()));
		ASSERT_THAT(IsTrue(Consensus.BeginAuthorization()));
		ASSERT_THAT(IsTrue(Consensus.BeginLaunch()));
	}

	TEST(BootstrapConsensusFailsClosedOnEquivocationOrPeerDisagreement,
		"SeinARTS.Unit.Network.Bootstrap")
	{
		const FSeinProtocolContext Context = BootstrapContext();
		const FSeinNetworkParticipantID A = BootstrapParticipant(1);
		const FSeinNetworkParticipantID B = BootstrapParticipant(2);

		FSeinBootstrapConsensus Equivocation;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(Equivocation.Configure(Context, {A, B}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::Accepted),
			AsInt(Equivocation.Submit(Context, A, BootstrapReceipt(1)))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::ConflictingRetry),
			AsInt(Equivocation.Submit(Context, A, BootstrapReceipt(2)))));
		ASSERT_THAT(AreEqual(1, Equivocation.GetSubmittedParticipantCount()));
		ASSERT_THAT(IsTrue(
			Equivocation.GetState() == ESeinBootstrapConsensusState::Failed));
		ASSERT_THAT(IsTrue(Equivocation.GetFailure()
			== ESeinBootstrapConsensusFailure::ConflictingRetry));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::AlreadyFailed),
			AsInt(Equivocation.Submit(
				Context, B, BootstrapReceipt(1)))));
		ASSERT_THAT(AreEqual(1, Equivocation.GetSubmittedParticipantCount()));
		ASSERT_THAT(IsTrue(Equivocation.GetFailure()
			== ESeinBootstrapConsensusFailure::ConflictingRetry));

		FSeinBootstrapConsensus Disagreement;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(Disagreement.Configure(Context, {A, B}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::Accepted),
			AsInt(Disagreement.Submit(Context, A, BootstrapReceipt(1)))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::ReceiptDisagreement),
			AsInt(Disagreement.Submit(Context, B, BootstrapReceipt(2)))));
		ASSERT_THAT(AreEqual(1, Disagreement.GetSubmittedParticipantCount()));
		FSeinMatchBootstrapReceipt NoAgreement;
		ASSERT_THAT(IsFalse(Disagreement.GetAgreedReceipt(NoAgreement)));
	}

	TEST(BootstrapConsensusFailsClosedOnWrongContractOrPhaseReceipt,
		"SeinARTS.Unit.Network.Bootstrap")
	{
		const FSeinProtocolContext Context = BootstrapContext();
		const FSeinNetworkParticipantID A = BootstrapParticipant(1);
		const FSeinNetworkParticipantID B = BootstrapParticipant(2);

		FSeinBootstrapConsensus WrongContract;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(WrongContract.Configure(Context, {A}))));
		FSeinMatchBootstrapReceipt ContractMismatch = BootstrapReceipt();
		ContractMismatch.ContractDigest =
			FGuid(0x76000000, 0x77000000, 0x78000000, 2);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::ContractDigestMismatch),
			AsInt(WrongContract.Submit(Context, A, ContractMismatch))));
		ASSERT_THAT(IsTrue(WrongContract.GetFailure()
			== ESeinBootstrapConsensusFailure::ContractDigestMismatch));

		FSeinBootstrapConsensus WrongSimulationContent;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(WrongSimulationContent.Configure(Context, {A}))));
		FSeinMatchBootstrapReceipt ContentMismatch = BootstrapReceipt();
		ContentMismatch.SimulationContentDigest =
			FGuid(0x7c000000, 0x7d000000, 0x7e000000, 2);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::
				SimulationContentDigestMismatch),
			AsInt(WrongSimulationContent.Submit(
				Context, A, ContentMismatch))));
		ASSERT_THAT(IsTrue(WrongSimulationContent.GetFailure()
			== ESeinBootstrapConsensusFailure::
				SimulationContentDigestMismatch));

		FSeinBootstrapConsensus WrongPhaseReceipt;
		const FSeinMatchBootstrapReceipt Receipt = BootstrapReceipt();
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(WrongPhaseReceipt.Configure(Context, {A, B}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::Accepted),
			AsInt(WrongPhaseReceipt.Submit(Context, A, Receipt))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::AgreementReached),
			AsInt(WrongPhaseReceipt.Submit(Context, B, Receipt))));
		ASSERT_THAT(IsTrue(WrongPhaseReceipt.BeginAuthorization()));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::ReceiptDisagreement),
			AsInt(WrongPhaseReceipt.SubmitAuthorizedReady(
				Context, A, BootstrapReceipt(2)))));
		ASSERT_THAT(IsTrue(WrongPhaseReceipt.GetFailure()
			== ESeinBootstrapConsensusFailure::PhaseReceiptMismatch));
		ASSERT_THAT(AreEqual(2,
			WrongPhaseReceipt.GetSubmittedParticipantCount()));
		FSeinMatchBootstrapReceipt RetainedAgreement;
		ASSERT_THAT(IsTrue(
			WrongPhaseReceipt.GetAgreedReceipt(RetainedAgreement)));
		ASSERT_THAT(IsTrue(RetainedAgreement == Receipt));
	}

	TEST(BootstrapConsensusPhaseEquivocationFailsAndRetainsEvidence,
		"SeinARTS.Unit.Network.Bootstrap")
	{
		const FSeinProtocolContext Context = BootstrapContext();
		const FSeinNetworkParticipantID A = BootstrapParticipant(1);
		const FSeinNetworkParticipantID B = BootstrapParticipant(2);
		const FSeinMatchBootstrapReceipt Receipt = BootstrapReceipt();

		FSeinBootstrapConsensus Consensus;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(Consensus.Configure(Context, {A, B}))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::Accepted),
			AsInt(Consensus.Submit(Context, A, Receipt))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::AgreementReached),
			AsInt(Consensus.Submit(Context, B, Receipt))));
		ASSERT_THAT(IsTrue(Consensus.BeginAuthorization()));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::Accepted),
			AsInt(Consensus.SubmitAuthorizedReady(Context, A, Receipt))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::ConflictingRetry),
			AsInt(Consensus.SubmitAuthorizedReady(
				Context, A, BootstrapReceipt(2)))));
		ASSERT_THAT(IsTrue(
			Consensus.GetState() == ESeinBootstrapConsensusState::Failed));
		ASSERT_THAT(IsTrue(Consensus.GetFailure()
			== ESeinBootstrapConsensusFailure::ConflictingRetry));
		ASSERT_THAT(AreEqual(2, Consensus.GetSubmittedParticipantCount()));
		ASSERT_THAT(AreEqual(1,
			Consensus.GetAuthorizedReadyParticipantCount()));

		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::AlreadyFailed),
			AsInt(Consensus.SubmitAuthorizedReady(Context, B, Receipt))));
		ASSERT_THAT(AreEqual(1,
			Consensus.GetAuthorizedReadyParticipantCount()));
		ASSERT_THAT(IsTrue(Consensus.GetFailure()
			== ESeinBootstrapConsensusFailure::ConflictingRetry));
	}

	TEST(BootstrapConsensusAuthenticatesIdentityAndExactContext,
		"SeinARTS.Unit.Network.Bootstrap")
	{
		FSeinBootstrapConsensus Consensus;
		const FSeinProtocolContext Context = BootstrapContext();
		const FSeinNetworkParticipantID SimPeer = BootstrapParticipant(2);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusConfigResult::Configured),
			AsInt(Consensus.Configure(Context, {SimPeer}))));

		FSeinProtocolContext WrongContext = Context;
		WrongContext.CoordinatorTerm = 2;
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::InvalidContext),
			AsInt(Consensus.Submit(
				WrongContext, SimPeer, BootstrapReceipt()))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::UnexpectedParticipant),
			AsInt(Consensus.Submit(
				Context, BootstrapParticipant(1), BootstrapReceipt()))));
		FSeinProtocolContext WrongDestination = Context;
		WrongDestination.DestinationWorldDigest =
			FGuid(0x73000000, 0x74000000, 0x75000000, 2);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::InvalidContext),
			AsInt(Consensus.Submit(
				WrongDestination, SimPeer, BootstrapReceipt()))));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinBootstrapConsensusSubmitResult::AgreementReached),
			AsInt(Consensus.Submit(Context, SimPeer, BootstrapReceipt()))));
	}
}
