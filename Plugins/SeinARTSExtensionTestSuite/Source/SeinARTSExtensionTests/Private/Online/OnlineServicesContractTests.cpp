/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         OnlineServicesContractTests.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Qualifies the SOS contract, loopback state, and facade lifecycle.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "TestGameInstance.h"

#include "Contract/SeinOnlineServicesContract.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Online/OnlineServicesTestTypes.h"
#include "Provider/SeinOnlineLoopbackProvider.h"
#include "SeinNetSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Subsystem/SeinOnlineServicesSubsystem.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::SeinARTSExtensionTests
{
	namespace
	{
		int32 AsInt(ESeinOnlineErrorCode Value)
		{
			return static_cast<int32>(Value);
		}

		int32 AsInt(ESeinOnlineRequestStatus Value)
		{
			return static_cast<int32>(Value);
		}

		FString UniqueKey(const TCHAR* Prefix)
		{
			return FString::Printf(
				TEXT("%s-%s"),
				Prefix,
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		}

		struct FExecutedRequest
		{
			bool bCompleted = false;
			FSeinOnlineProviderResponse Response;
		};

		struct FLoopbackFixture
		{
			FActorTestSpawner Spawner;
			USeinOnlineServicesSubsystem* Owner = nullptr;
			USeinOnlineLoopbackProvider* Provider = nullptr;
			int64 NextHandle = 1;

			bool Initialize()
			{
				Spawner.InitializeGameSubsystems();
				UGameInstance* GameInstance = Spawner.GetGameInstance();
				USeinOnlineServicesSubsystem* ExistingOwner = GameInstance
					? GameInstance->GetSubsystem<USeinOnlineServicesSubsystem>()
					: nullptr;
				return ExistingOwner && Initialize(*ExistingOwner);
			}

			bool Initialize(USeinOnlineServicesSubsystem& ExistingOwner)
			{
				Owner = &ExistingOwner;
				Provider = NewObject<USeinOnlineLoopbackProvider>(Owner);
				FSeinOnlineError Error;
				return Provider && Provider->Initialize(*Owner, Error)
					&& Error.IsSuccess();
			}

			~FLoopbackFixture()
			{
				if (Provider)
				{
					Provider->Shutdown();
				}
			}

			template <typename RequestType>
			FExecutedRequest Execute(
				ESeinOnlineOperation Operation,
				RequestType Request,
				ESeinOnlineCallerAuthority Authority =
					ESeinOnlineCallerAuthority::Client)
			{
				FSeinOnlineProviderRequest Envelope;
				Envelope.Handle.Value = NextHandle++;
				Envelope.Operation = Operation;
				Envelope.Authority = Authority;
				Envelope.Payload = FInstancedStruct::Make(MoveTemp(Request));

				FExecutedRequest Executed;
				Provider->BeginRequest(
					Envelope,
					[&Executed](FSeinOnlineProviderResponse&& Response)
					{
						Executed.bCompleted = true;
						Executed.Response = MoveTemp(Response);
					});
				return Executed;
			}

			FSeinOnlineOpaqueID Authenticate(
				int32 LocalUserIndex,
				const TCHAR* DisplayName,
				FString Credential = FString())
			{
				FSeinOnlineAuthenticateRequest Request;
				Request.LocalUserIndex = LocalUserIndex;
				Request.DisplayNameHint = DisplayName;
				Request.Credential = MoveTemp(Credential);
				const FExecutedRequest Executed = Execute(
					ESeinOnlineOperation::Authenticate, MoveTemp(Request));
				const FSeinOnlineAuthenticateResult* Result =
					Executed.Response.Payload.GetPtr<
						FSeinOnlineAuthenticateResult>();
				return Executed.bCompleted
					&& Executed.Response.Error.IsSuccess() && Result
					? Result->Account.AccountID
					: FSeinOnlineOpaqueID();
			}
		};

		struct FRegisteredMatch
		{
			FSeinOnlineOpaqueID AccountOne;
			FSeinOnlineOpaqueID AccountTwo;
			FSeinOnlineOpaqueID PartyID;
			FSeinOnlineOpaqueID TicketID;
			FSeinOnlineOpaqueID AllocationID;
			FString AllocationLeaseSecret;
			FSeinMatchInstanceID MatchID;
			FSeinNetworkParticipantID ParticipantOne;
			FSeinNetworkParticipantID ParticipantTwo;
		};

		bool BuildRegisteredMatch(
			FLoopbackFixture& Fixture,
			ESeinOnlineMatchClassification Classification,
			FRegisteredMatch& Out)
		{
			Out.AccountOne = Fixture.Authenticate(1101, TEXT("Alpha"));
			Out.AccountTwo = Fixture.Authenticate(1102, TEXT("Bravo"));
			if (!Out.AccountOne.IsValid() || !Out.AccountTwo.IsValid())
			{
				return false;
			}

			FSeinOnlineCreatePartyRequest CreateParty;
			CreateParty.LeaderAccountID = Out.AccountOne;
			CreateParty.MaxMembers = 2;
			CreateParty.IdempotencyKey = UniqueKey(TEXT("party-create"));
			const FExecutedRequest Created = Fixture.Execute(
				ESeinOnlineOperation::CreateParty, CreateParty);
			const FSeinOnlinePartyResult* CreatedParty =
				Created.Response.Payload.GetPtr<FSeinOnlinePartyResult>();
			if (!Created.bCompleted || !Created.Response.Error.IsSuccess()
				|| !CreatedParty)
			{
				return false;
			}
			Out.PartyID = CreatedParty->Party.PartyID;
			const FExecutedRequest CreatedRetry = Fixture.Execute(
				ESeinOnlineOperation::CreateParty, CreateParty);
			const FSeinOnlinePartyResult* RetriedParty =
				CreatedRetry.Response.Payload.GetPtr<FSeinOnlinePartyResult>();
			if (!RetriedParty || RetriedParty->Party.PartyID != Out.PartyID)
			{
				return false;
			}

			FSeinOnlineInviteToPartyRequest Invite;
			Invite.PartyID = Out.PartyID;
			Invite.InviterAccountID = Out.AccountOne;
			Invite.InviteeAccountID = Out.AccountTwo;
			const FExecutedRequest Invited = Fixture.Execute(
				ESeinOnlineOperation::InviteToParty, Invite);
			const FSeinOnlinePartyInviteResult* InviteResult =
				Invited.Response.Payload.GetPtr<FSeinOnlinePartyInviteResult>();
			if (!Invited.bCompleted || !Invited.Response.Error.IsSuccess()
				|| !InviteResult)
			{
				return false;
			}

			FSeinOnlineJoinPartyRequest Join;
			Join.InviteID = InviteResult->InviteID;
			Join.AccountID = Out.AccountTwo;
			const FExecutedRequest Joined = Fixture.Execute(
				ESeinOnlineOperation::JoinParty, Join);
			const FSeinOnlinePartyResult* JoinedParty =
				Joined.Response.Payload.GetPtr<FSeinOnlinePartyResult>();
			if (!Joined.bCompleted || !Joined.Response.Error.IsSuccess()
				|| !JoinedParty || JoinedParty->Party.Members.Num() != 2)
			{
				return false;
			}

			FSeinOnlineStartMatchmakingRequest Start;
			Start.AccountID = Out.AccountOne;
			Start.PartyID = Out.PartyID;
			Start.Queue = TEXT("Qualification");
			Start.Region = TEXT("Local");
			Start.IdempotencyKey = UniqueKey(TEXT("matchmaking-start"));
			const FExecutedRequest Started = Fixture.Execute(
				ESeinOnlineOperation::StartMatchmaking, Start);
			const FSeinOnlineMatchmakingResult* Ticket =
				Started.Response.Payload.GetPtr<FSeinOnlineMatchmakingResult>();
			if (!Started.bCompleted || !Started.Response.Error.IsSuccess()
				|| !Ticket)
			{
				return false;
			}
			Out.TicketID = Ticket->Ticket.TicketID;
			const FExecutedRequest StartedRetry = Fixture.Execute(
				ESeinOnlineOperation::StartMatchmaking, Start);
			const FSeinOnlineMatchmakingResult* RetriedTicket =
				StartedRetry.Response.Payload.GetPtr<
					FSeinOnlineMatchmakingResult>();
			if (!RetriedTicket
				|| RetriedTicket->Ticket.TicketID != Out.TicketID)
			{
				return false;
			}

			FSeinOnlineAllocateServerRequest Allocate;
			Allocate.TicketID = Out.TicketID;
			Allocate.BuildID = TEXT("test-build");
			Allocate.IdempotencyKey = UniqueKey(TEXT("server-allocation"));
			const FExecutedRequest Allocated = Fixture.Execute(
				ESeinOnlineOperation::AllocateServer,
				Allocate,
				ESeinOnlineCallerAuthority::TrustedServer);
			const FSeinOnlineServerAllocationResult* Allocation =
				Allocated.Response.Payload.GetPtr<
					FSeinOnlineServerAllocationResult>();
			if (!Allocated.bCompleted || !Allocated.Response.Error.IsSuccess()
				|| !Allocation)
			{
				return false;
			}
			Out.AllocationID = Allocation->AllocationID;
			Out.AllocationLeaseSecret = Allocation->LeaseSecret;
			const FExecutedRequest AllocatedRetry = Fixture.Execute(
				ESeinOnlineOperation::AllocateServer,
				Allocate,
				ESeinOnlineCallerAuthority::TrustedServer);
			const FSeinOnlineServerAllocationResult* RetriedAllocation =
				AllocatedRetry.Response.Payload.GetPtr<
					FSeinOnlineServerAllocationResult>();
			if (!RetriedAllocation
				|| RetriedAllocation->AllocationID != Out.AllocationID
				|| RetriedAllocation->LeaseSecret
					!= Out.AllocationLeaseSecret)
			{
				return false;
			}
			Allocate.IdempotencyKey = UniqueKey(TEXT("server-allocation-equivalent"));
			const FExecutedRequest EquivalentAllocation = Fixture.Execute(
				ESeinOnlineOperation::AllocateServer,
				Allocate,
				ESeinOnlineCallerAuthority::TrustedServer);
			const FSeinOnlineServerAllocationResult* EquivalentAllocationResult =
				EquivalentAllocation.Response.Payload.GetPtr<
					FSeinOnlineServerAllocationResult>();
			if (!EquivalentAllocationResult
				|| EquivalentAllocationResult->AllocationID != Out.AllocationID
				|| EquivalentAllocationResult->LeaseSecret
					!= Out.AllocationLeaseSecret)
			{
				return false;
			}
			Allocate.BuildID = TEXT("different-build");
			Allocate.IdempotencyKey = UniqueKey(TEXT("server-allocation-conflict"));
			const FExecutedRequest ConflictingAllocation = Fixture.Execute(
				ESeinOnlineOperation::AllocateServer,
				Allocate,
				ESeinOnlineCallerAuthority::TrustedServer);
			if (ConflictingAllocation.Response.Error.Code
				!= ESeinOnlineErrorCode::Conflict)
			{
				return false;
			}

			Out.MatchID = FSeinMatchInstanceID(FGuid::NewGuid());
			Out.ParticipantOne = FSeinNetworkParticipantID(FGuid::NewGuid());
			Out.ParticipantTwo = FSeinNetworkParticipantID(FGuid::NewGuid());
			FSeinOnlineRegisterMatchRequest Register;
			Register.MatchID = Out.MatchID;
			Register.AllocationID = Out.AllocationID;
			Register.Classification = Classification;
			Register.IdempotencyKey = UniqueKey(TEXT("match-register"));
			FSeinOnlineMatchRosterEntry& One =
				Register.Roster.AddDefaulted_GetRef();
			One.AccountID = Out.AccountOne;
			One.ParticipantID = Out.ParticipantOne;
			One.PlayerID = FSeinPlayerID(1);
			FSeinOnlineMatchRosterEntry& Two =
				Register.Roster.AddDefaulted_GetRef();
			Two.AccountID = Out.AccountTwo;
			Two.ParticipantID = Out.ParticipantTwo;
			Two.PlayerID = FSeinPlayerID(2);
			const FExecutedRequest Registered = Fixture.Execute(
				ESeinOnlineOperation::RegisterMatch,
				Register,
				ESeinOnlineCallerAuthority::TrustedServer);
			const FExecutedRequest RegisteredRetry = Fixture.Execute(
				ESeinOnlineOperation::RegisterMatch,
				Register,
				ESeinOnlineCallerAuthority::TrustedServer);
			Register.IdempotencyKey = UniqueKey(TEXT("match-register-equivalent"));
			const FExecutedRequest EquivalentRegistration = Fixture.Execute(
				ESeinOnlineOperation::RegisterMatch,
				Register,
				ESeinOnlineCallerAuthority::TrustedServer);
			Register.MatchID = FSeinMatchInstanceID(FGuid::NewGuid());
			Register.IdempotencyKey = UniqueKey(TEXT("match-register-conflict"));
			const FExecutedRequest ConflictingRegistration = Fixture.Execute(
				ESeinOnlineOperation::RegisterMatch,
				Register,
				ESeinOnlineCallerAuthority::TrustedServer);
			return Registered.bCompleted
				&& Registered.Response.Error.IsSuccess()
				&& RegisteredRetry.Response.Error.IsSuccess()
				&& EquivalentRegistration.Response.Error.IsSuccess()
				&& ConflictingRegistration.Response.Error.Code
					== ESeinOnlineErrorCode::Conflict
				&& Registered.Response.Payload.GetPtr<
					FSeinOnlineRegisterMatchResult>();
		}
	}

	TEST(OnlineServicesContractRejectsSchemaAndBoundsDrift,
		"SeinARTS.Unit.OnlineServices.Contract")
	{
		for (int32 Raw = static_cast<int32>(ESeinOnlineOperation::Authenticate);
			Raw <= static_cast<int32>(ESeinOnlineOperation::SubmitTelemetry);
			++Raw)
		{
			const ESeinOnlineOperation Operation =
				static_cast<ESeinOnlineOperation>(Raw);
			ASSERT_THAT(IsNotNull(
				SeinOnlineContract::GetRequestStruct(Operation)));
			ASSERT_THAT(IsNotNull(
				SeinOnlineContract::GetResultStruct(Operation)));
			ASSERT_THAT(IsFalse(
				SeinOnlineContract::GetOperationName(Operation).IsEmpty()));
		}

		FSeinOnlineAuthenticateRequest Authentication;
		Authentication.LocalUserIndex = 0;
		FSeinOnlineProviderRequest Request;
		Request.Handle.Value = 1;
		Request.Operation = ESeinOnlineOperation::Authenticate;
		Request.Payload = FInstancedStruct::Make(Authentication);
		FSeinOnlineError Error;
		ASSERT_THAT(IsTrue(
			SeinOnlineContract::ValidateRequest(Request, Error)));

		Request.Operation = ESeinOnlineOperation::CreateParty;
		ASSERT_THAT(IsFalse(
			SeinOnlineContract::ValidateRequest(Request, Error)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::InvalidRequest), AsInt(Error.Code)));

		Request.Operation = ESeinOnlineOperation::Authenticate;
		Authentication.DisplayNameHint = FString::ChrN(
			SeinOnlineServicesContract::MaxDisplayNameLength + 1, TEXT('x'));
		Request.Payload = FInstancedStruct::Make(Authentication);
		ASSERT_THAT(IsFalse(
			SeinOnlineContract::ValidateRequest(Request, Error)));

		Request.Payload = FInstancedStruct::Make(
			FSeinOnlineAuthenticateRequest());
		FSeinOnlineProviderResponse Response;
		Response.Handle.Value = 2;
		Response.Operation = ESeinOnlineOperation::Authenticate;
		Response.Payload = FInstancedStruct::Make(
			FSeinOnlineAuthenticateResult());
		ASSERT_THAT(IsFalse(
			SeinOnlineContract::ValidateResponse(Request, Response, Error)));

		Response.Handle = Request.Handle;
		Response.Error = FSeinOnlineError::Make(
			ESeinOnlineErrorCode::NotFound, TEXT("not found"));
		ASSERT_THAT(IsFalse(
			SeinOnlineContract::ValidateResponse(Request, Response, Error)));

		Response.Error = FSeinOnlineError();
		FSeinOnlineAuthenticateResult OversizedResult;
		OversizedResult.Account.AccountID.Provider = TEXT("Loopback");
		OversizedResult.Account.AccountID.Value = TEXT("account");
		OversizedResult.Account.DisplayName = FString::ChrN(
			SeinOnlineServicesContract::MaxDisplayNameLength + 1, TEXT('x'));
		OversizedResult.Account.LocalUserIndex = 0;
		Response.Payload = FInstancedStruct::Make(OversizedResult);
		ASSERT_THAT(IsFalse(
			SeinOnlineContract::ValidateResponse(Request, Response, Error)));

		FSeinOnlineSubmitMatchResultRequest InvalidPlacement;
		InvalidPlacement.MatchID = FSeinMatchInstanceID(FGuid::NewGuid());
		InvalidPlacement.TerminalTick = 1;
		InvalidPlacement.FinalWorldRoot = FGuid::NewGuid();
		InvalidPlacement.ReplayFinalDigest = FGuid::NewGuid();
		InvalidPlacement.ReplayEvidenceID.Provider = TEXT("Loopback");
		InvalidPlacement.ReplayEvidenceID.Value = TEXT("replay");
		InvalidPlacement.IdempotencyKey = TEXT("placement-bounds");
		FSeinOnlineMatchPlacement& Placement =
			InvalidPlacement.Placements.AddDefaulted_GetRef();
		Placement.AccountID.Provider = TEXT("Loopback");
		Placement.AccountID.Value = TEXT("account");
		Placement.Placement = 2;
		Request.Operation = ESeinOnlineOperation::SubmitMatchResult;
		Request.Payload = FInstancedStruct::Make(InvalidPlacement);
		ASSERT_THAT(IsFalse(
			SeinOnlineContract::ValidateRequest(Request, Error)));
	}

	TEST(OnlineServicesLoopbackPolicyFailsClosed,
		"SeinARTS.Unit.OnlineServices.RuntimePolicy")
	{
		ASSERT_THAT(IsTrue(
			USeinOnlineServicesSubsystem::IsDevelopmentLoopbackPermittedForTests(
				true, false, false, false)));
		ASSERT_THAT(IsFalse(
			USeinOnlineServicesSubsystem::IsDevelopmentLoopbackPermittedForTests(
				false, false, false, false)));
		ASSERT_THAT(IsFalse(
			USeinOnlineServicesSubsystem::IsDevelopmentLoopbackPermittedForTests(
				true, true, false, false)));
		ASSERT_THAT(IsFalse(
			USeinOnlineServicesSubsystem::IsDevelopmentLoopbackPermittedForTests(
				true, false, true, false)));
		ASSERT_THAT(IsFalse(
			USeinOnlineServicesSubsystem::IsDevelopmentLoopbackPermittedForTests(
				true, false, false, true)));
	}

	TEST(LoopbackRankedFlowIsEvidenceBoundAndIdempotent,
		"SeinARTS.Integration.OnlineServices.Loopback")
	{
		FLoopbackFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));
		FRegisteredMatch Match;
		ASSERT_THAT(IsTrue(BuildRegisteredMatch(
			Fixture, ESeinOnlineMatchClassification::Ranked, Match)));

		FSeinOnlinePublishReplayEvidenceRequest Publish;
		Publish.MatchID = Match.MatchID;
		Publish.FinalWorldRoot = FGuid::NewGuid();
		Publish.TerminalTick = 900;
		Publish.ReplayFinalDigest = FGuid::NewGuid();
		Publish.Evidence = {1, 2, 3, 4};
		Publish.IdempotencyKey = UniqueKey(TEXT("replay"));
		const FExecutedRequest ClientPublish = Fixture.Execute(
			ESeinOnlineOperation::PublishReplayEvidence, Publish);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Forbidden),
			AsInt(ClientPublish.Response.Error.Code)));
		const FExecutedRequest Published = Fixture.Execute(
			ESeinOnlineOperation::PublishReplayEvidence, Publish,
			ESeinOnlineCallerAuthority::TrustedServer);
		ASSERT_THAT(IsTrue(Published.bCompleted));
		ASSERT_THAT(IsTrue(Published.Response.Error.IsSuccess()));
		const FSeinOnlinePublishReplayEvidenceResult* Replay =
			Published.Response.Payload.GetPtr<
				FSeinOnlinePublishReplayEvidenceResult>();
		ASSERT_THAT(IsNotNull(Replay));

		const FExecutedRequest ReplayRetry = Fixture.Execute(
			ESeinOnlineOperation::PublishReplayEvidence, Publish,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlinePublishReplayEvidenceResult* RetriedReplay =
			ReplayRetry.Response.Payload.GetPtr<
				FSeinOnlinePublishReplayEvidenceResult>();
		ASSERT_THAT(IsNotNull(RetriedReplay));
		ASSERT_THAT(IsTrue(
			RetriedReplay->Replay.ReplayID == Replay->Replay.ReplayID));

		FSeinOnlineSubmitMatchResultRequest Submit;
		Submit.MatchID = Match.MatchID;
		Submit.TerminalTick = Publish.TerminalTick;
		Submit.FinalWorldRoot = Publish.FinalWorldRoot;
		Submit.ReplayFinalDigest = Publish.ReplayFinalDigest;
		Submit.ReplayEvidenceID = Replay->Replay.ReplayID;
		Submit.IdempotencyKey = UniqueKey(TEXT("result"));
		FSeinOnlineMatchPlacement& First =
			Submit.Placements.AddDefaulted_GetRef();
		First.AccountID = Match.AccountOne;
		First.Placement = 1;
		FSeinOnlineMatchPlacement& Second =
			Submit.Placements.AddDefaulted_GetRef();
		Second.AccountID = Match.AccountTwo;
		Second.Placement = 2;

		const FExecutedRequest ClientSubmit = Fixture.Execute(
			ESeinOnlineOperation::SubmitMatchResult, Submit);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Forbidden),
			AsInt(ClientSubmit.Response.Error.Code)));

		const FExecutedRequest TrustedSubmit = Fixture.Execute(
			ESeinOnlineOperation::SubmitMatchResult,
			Submit,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineMutationReceipt* Receipt =
			TrustedSubmit.Response.Payload.GetPtr<FSeinOnlineMutationReceipt>();
		ASSERT_THAT(IsNotNull(Receipt));
		ASSERT_THAT(IsTrue(Receipt->ReceiptID.IsValid()));

		const FExecutedRequest TrustedRetry = Fixture.Execute(
			ESeinOnlineOperation::SubmitMatchResult,
			Submit,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineMutationReceipt* RetryReceipt =
			TrustedRetry.Response.Payload.GetPtr<FSeinOnlineMutationReceipt>();
		ASSERT_THAT(IsNotNull(RetryReceipt));
		ASSERT_THAT(IsTrue(RetryReceipt->ReceiptID == Receipt->ReceiptID));
		const FExecutedRequest UntrustedRetry = Fixture.Execute(
			ESeinOnlineOperation::SubmitMatchResult, Submit);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Forbidden),
			AsInt(UntrustedRetry.Response.Error.Code)));

		Swap(Submit.Placements[0].Placement, Submit.Placements[1].Placement);
		const FExecutedRequest ConflictingRetry = Fixture.Execute(
			ESeinOnlineOperation::SubmitMatchResult,
			Submit,
			ESeinOnlineCallerAuthority::TrustedServer);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Conflict),
			AsInt(ConflictingRetry.Response.Error.Code)));
		Swap(Submit.Placements[0].Placement, Submit.Placements[1].Placement);
		Submit.IdempotencyKey = UniqueKey(TEXT("result-equivalent"));
		const FExecutedRequest EquivalentResult = Fixture.Execute(
			ESeinOnlineOperation::SubmitMatchResult,
			Submit,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineMutationReceipt* EquivalentReceipt =
			EquivalentResult.Response.Payload.GetPtr<
				FSeinOnlineMutationReceipt>();
		ASSERT_THAT(IsNotNull(EquivalentReceipt));
		ASSERT_THAT(IsTrue(
			EquivalentReceipt->ReceiptID == Receipt->ReceiptID));
		Swap(Submit.Placements[0].Placement, Submit.Placements[1].Placement);
		Submit.IdempotencyKey = UniqueKey(TEXT("result-conflicting-terminal"));
		const FExecutedRequest ConflictingTerminal = Fixture.Execute(
			ESeinOnlineOperation::SubmitMatchResult,
			Submit,
			ESeinOnlineCallerAuthority::TrustedServer);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Conflict),
			AsInt(ConflictingTerminal.Response.Error.Code)));

		FSeinOnlineWriteStatsRequest Write;
		Write.AccountID = Match.AccountOne;
		Write.MatchID = Match.MatchID;
		Write.IdempotencyKey = UniqueKey(TEXT("stats"));
		FSeinOnlineStatMutation& Mutation =
			Write.Mutations.AddDefaulted_GetRef();
		Mutation.Stat = TEXT("Rating");
		Mutation.Kind = ESeinOnlineStatMutationKind::Add;
		Mutation.Value = 25;
		const FExecutedRequest ClientWrite = Fixture.Execute(
			ESeinOnlineOperation::WriteStats, Write);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Forbidden),
			AsInt(ClientWrite.Response.Error.Code)));

		const FExecutedRequest TrustedWrite = Fixture.Execute(
			ESeinOnlineOperation::WriteStats, Write,
			ESeinOnlineCallerAuthority::TrustedServer);
		ASSERT_THAT(IsTrue(TrustedWrite.Response.Error.IsSuccess()));
		const FExecutedRequest StatsRetry = Fixture.Execute(
			ESeinOnlineOperation::WriteStats, Write,
			ESeinOnlineCallerAuthority::TrustedServer);
		ASSERT_THAT(IsTrue(StatsRetry.Response.Error.IsSuccess()));
		const FExecutedRequest UntrustedStatsRetry = Fixture.Execute(
			ESeinOnlineOperation::WriteStats, Write);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Forbidden),
			AsInt(UntrustedStatsRetry.Response.Error.Code)));

		FSeinOnlineQueryStatsRequest Query;
		Query.AccountID = Match.AccountOne;
		const FExecutedRequest Queried = Fixture.Execute(
			ESeinOnlineOperation::QueryStats, Query);
		const FSeinOnlineQueryStatsResult* Stats =
			Queried.Response.Payload.GetPtr<FSeinOnlineQueryStatsResult>();
		ASSERT_THAT(IsNotNull(Stats));
		ASSERT_THAT(AreEqual(1, Stats->Stats.Num()));
		ASSERT_THAT(AreEqual(static_cast<int64>(25), Stats->Stats[0].Value));

		FSeinOnlineWriteStatsRequest OtherWrite = Write;
		OtherWrite.AccountID = Match.AccountTwo;
		OtherWrite.Mutations[0].Value = 10;
		ASSERT_THAT(IsTrue(Fixture.Execute(
			ESeinOnlineOperation::WriteStats,
			OtherWrite,
			ESeinOnlineCallerAuthority::TrustedServer)
			.Response.Error.IsSuccess()));

		FSeinOnlineQueryLeaderboardRequest Leaderboard;
		Leaderboard.Stat = TEXT("Rating");
		Leaderboard.Limit = 10;
		const FExecutedRequest Ranked = Fixture.Execute(
			ESeinOnlineOperation::QueryLeaderboard, Leaderboard);
		const FSeinOnlineQueryLeaderboardResult* Rows =
			Ranked.Response.Payload.GetPtr<
				FSeinOnlineQueryLeaderboardResult>();
		ASSERT_THAT(IsNotNull(Rows));
		ASSERT_THAT(AreEqual(2, Rows->Entries.Num()));
		ASSERT_THAT(IsTrue(Rows->Entries[0].AccountID == Match.AccountOne));
		ASSERT_THAT(AreEqual(static_cast<int64>(25), Rows->Entries[0].Score));
		ASSERT_THAT(AreEqual(1, Rows->Entries[0].Rank));
	}

	TEST(LoopbackCampaignSavesUseCASAndSharedProviderState,
		"SeinARTS.Integration.OnlineServices.Persistence")
	{
		FLoopbackFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));
		const FString OwnerCredential = UniqueKey(TEXT("save-owner-credential"));
		const FString OtherCredential = UniqueKey(TEXT("save-other-credential"));
		const FSeinOnlineOpaqueID OwnerAccount = Fixture.Authenticate(
			1201, TEXT("Save Owner"), OwnerCredential);
		const FSeinOnlineOpaqueID OtherAccount =
			Fixture.Authenticate(1202, TEXT("Other Account"), OtherCredential);
		ASSERT_THAT(IsTrue(OwnerAccount.IsValid()));
		ASSERT_THAT(IsTrue(OtherAccount.IsValid()));

		FSeinOnlineWriteCampaignSaveRequest Create;
		Create.RequestingAccountID = OwnerAccount;
		Create.Owner.Kind = ESeinOnlineSaveOwnerKind::Account;
		Create.Owner.OwnerID = OwnerAccount;
		Create.Slot = UniqueKey(TEXT("campaign-slot"));
		Create.Mode = ESeinOnlineSaveWriteMode::CreateOnly;
		Create.Data = {1, 2, 3};
		Create.IdempotencyKey = UniqueKey(TEXT("save-create"));
		const FExecutedRequest Created = Fixture.Execute(
			ESeinOnlineOperation::WriteCampaignSave, Create);
		const FSeinOnlineCampaignSaveResult* CreatedSave =
			Created.Response.Payload.GetPtr<FSeinOnlineCampaignSaveResult>();
		ASSERT_THAT(IsNotNull(CreatedSave));
		ASSERT_THAT(AreEqual(static_cast<int64>(1), CreatedSave->Save.Revision));

		const FExecutedRequest CreateRetry = Fixture.Execute(
			ESeinOnlineOperation::WriteCampaignSave, Create);
		const FSeinOnlineCampaignSaveResult* RetriedSave =
			CreateRetry.Response.Payload.GetPtr<FSeinOnlineCampaignSaveResult>();
		ASSERT_THAT(IsNotNull(RetriedSave));
		ASSERT_THAT(AreEqual(static_cast<int64>(1), RetriedSave->Save.Revision));

		Create.Data.Add(4);
		const FExecutedRequest ChangedRetry = Fixture.Execute(
			ESeinOnlineOperation::WriteCampaignSave, Create);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Conflict),
			AsInt(ChangedRetry.Response.Error.Code)));

		FSeinOnlineWriteCampaignSaveRequest Update = Create;
		Update.Mode = ESeinOnlineSaveWriteMode::IfRevision;
		Update.ExpectedRevision = 1;
		Update.Data = {9, 8, 7};
		Update.IdempotencyKey = UniqueKey(TEXT("save-update"));
		const FExecutedRequest Updated = Fixture.Execute(
			ESeinOnlineOperation::WriteCampaignSave, Update);
		const FSeinOnlineCampaignSaveResult* UpdatedSave =
			Updated.Response.Payload.GetPtr<FSeinOnlineCampaignSaveResult>();
		ASSERT_THAT(IsNotNull(UpdatedSave));
		ASSERT_THAT(AreEqual(static_cast<int64>(2), UpdatedSave->Save.Revision));

		Update.IdempotencyKey = UniqueKey(TEXT("save-stale"));
		const FExecutedRequest Stale = Fixture.Execute(
			ESeinOnlineOperation::WriteCampaignSave, Update);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Conflict),
			AsInt(Stale.Response.Error.Code)));

		USeinOnlineLoopbackProvider* SecondProvider =
			NewObject<USeinOnlineLoopbackProvider>(Fixture.Owner);
		FSeinOnlineError InitializeError;
		ASSERT_THAT(IsTrue(SecondProvider->Initialize(
			*Fixture.Owner, InitializeError)));
		FSeinOnlineAuthenticateRequest SecondAuthentication;
		SecondAuthentication.LocalUserIndex = 0;
		SecondAuthentication.Credential = OwnerCredential;
		FSeinOnlineProviderRequest AuthenticationEnvelope;
		AuthenticationEnvelope.Handle.Value = 999000;
		AuthenticationEnvelope.Operation = ESeinOnlineOperation::Authenticate;
		AuthenticationEnvelope.Payload =
			FInstancedStruct::Make(SecondAuthentication);
		FSeinOnlineProviderResponse AuthenticationResponse;
		SecondProvider->BeginRequest(
			AuthenticationEnvelope,
			[&](FSeinOnlineProviderResponse&& Response)
			{
				AuthenticationResponse = MoveTemp(Response);
			});
		const FSeinOnlineAuthenticateResult* SecondSession =
			AuthenticationResponse.Payload.GetPtr<
				FSeinOnlineAuthenticateResult>();
		ASSERT_THAT(IsNotNull(SecondSession));
		ASSERT_THAT(IsTrue(
			SecondSession->Account.AccountID == OwnerAccount));
		SecondAuthentication.LocalUserIndex = 1;
		SecondAuthentication.Credential = OtherCredential;
		AuthenticationEnvelope.Handle.Value++;
		AuthenticationEnvelope.Payload =
			FInstancedStruct::Make(SecondAuthentication);
		SecondProvider->BeginRequest(
			AuthenticationEnvelope,
			[&](FSeinOnlineProviderResponse&& Response)
			{
				AuthenticationResponse = MoveTemp(Response);
			});
		const FSeinOnlineAuthenticateResult* OtherSession =
			AuthenticationResponse.Payload.GetPtr<
				FSeinOnlineAuthenticateResult>();
		ASSERT_THAT(IsNotNull(OtherSession));
		ASSERT_THAT(IsTrue(
			OtherSession->Account.AccountID == OtherAccount));
		FSeinOnlineReadCampaignSaveRequest Read;
		Read.RequestingAccountID = OwnerAccount;
		Read.Owner = Update.Owner;
		Read.Slot = Update.Slot;
		FSeinOnlineProviderRequest ReadEnvelope;
		ReadEnvelope.Handle.Value = 999001;
		ReadEnvelope.Operation = ESeinOnlineOperation::ReadCampaignSave;
		ReadEnvelope.Payload = FInstancedStruct::Make(Read);
		FSeinOnlineProviderResponse ReadResponse;
		bool bReadCompleted = false;
		SecondProvider->BeginRequest(
			ReadEnvelope,
			[&](FSeinOnlineProviderResponse&& Response)
			{
				bReadCompleted = true;
				ReadResponse = MoveTemp(Response);
			});
		ASSERT_THAT(IsTrue(bReadCompleted));
		const FSeinOnlineCampaignSaveResult* ReadSave =
			ReadResponse.Payload.GetPtr<FSeinOnlineCampaignSaveResult>();
		ASSERT_THAT(IsNotNull(ReadSave));
		ASSERT_THAT(AreEqual(static_cast<int64>(2), ReadSave->Save.Revision));
		ASSERT_THAT(AreEqual(3, ReadSave->Save.Data.Num()));

		Read.RequestingAccountID = OtherAccount;
		ReadEnvelope.Handle.Value++;
		ReadEnvelope.Payload = FInstancedStruct::Make(Read);
		SecondProvider->BeginRequest(
			ReadEnvelope,
			[&](FSeinOnlineProviderResponse&& Response)
			{
				ReadResponse = MoveTemp(Response);
			});
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Forbidden),
			AsInt(ReadResponse.Error.Code)));
		SecondProvider->Shutdown();
	}

	TEST(LoopbackReconnectAndTelemetryFailClosed,
		"SeinARTS.Integration.OnlineServices.Security")
	{
		FLoopbackFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));
		FRegisteredMatch Match;
		ASSERT_THAT(IsTrue(BuildRegisteredMatch(
			Fixture, ESeinOnlineMatchClassification::Unranked, Match)));
		Fixture.Provider->SetUtcNowUnixMillisecondsForTests(1000);

		FSeinOnlineIssueReconnectCredentialRequest Issue;
		Issue.MatchID = Match.MatchID;
		Issue.ParticipantID = Match.ParticipantOne;
		Issue.LifetimeSeconds = 1;
		Issue.IdempotencyKey = UniqueKey(TEXT("credential-expiring"));
		const FExecutedRequest Issued = Fixture.Execute(
			ESeinOnlineOperation::IssueReconnectCredential,
			Issue,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineReconnectCredentialResult* Credential =
			Issued.Response.Payload.GetPtr<
				FSeinOnlineReconnectCredentialResult>();
		ASSERT_THAT(IsNotNull(Credential));
		ASSERT_THAT(AreEqual(static_cast<int64>(2000),
			Credential->ExpiresAtUnixMilliseconds));
		const FExecutedRequest IssueRetry = Fixture.Execute(
			ESeinOnlineOperation::IssueReconnectCredential,
			Issue,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineReconnectCredentialResult* RetriedCredential =
			IssueRetry.Response.Payload.GetPtr<
				FSeinOnlineReconnectCredentialResult>();
		ASSERT_THAT(IsNotNull(RetriedCredential));
		ASSERT_THAT(AreEqual(
			Credential->Credential, RetriedCredential->Credential));
		ASSERT_THAT(AreEqual(
			Credential->AdmissionID, RetriedCredential->AdmissionID));

		FSeinOnlineValidateReconnectCredentialRequest Validate;
		Validate.MatchID = Match.MatchID;
		Validate.ParticipantID = Match.ParticipantOne;
		Validate.Credential = Credential->Credential;
		Fixture.Provider->SetUtcNowUnixMillisecondsForTests(1999);
		ASSERT_THAT(IsTrue(Fixture.Execute(
			ESeinOnlineOperation::ValidateReconnectCredential,
			Validate,
			ESeinOnlineCallerAuthority::TrustedServer)
			.Response.Error.IsSuccess()));
		Fixture.Provider->SetUtcNowUnixMillisecondsForTests(2000);
		const FExecutedRequest Expired = Fixture.Execute(
			ESeinOnlineOperation::ValidateReconnectCredential,
			Validate,
			ESeinOnlineCallerAuthority::TrustedServer);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Expired),
			AsInt(Expired.Response.Error.Code)));

		Fixture.Provider->SetUtcNowUnixMillisecondsForTests(3000);
		Issue.IdempotencyKey = UniqueKey(TEXT("credential-fresh"));
		const FExecutedRequest Reissued = Fixture.Execute(
			ESeinOnlineOperation::IssueReconnectCredential,
			Issue,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineReconnectCredentialResult* Fresh =
			Reissued.Response.Payload.GetPtr<
				FSeinOnlineReconnectCredentialResult>();
		ASSERT_THAT(IsNotNull(Fresh));

		FSeinOnlineConnectionAdmissionRequest Admission;
		Admission.AdmissionID = Fresh->AdmissionID;
		ASSERT_THAT(IsTrue(
			Fixture.Provider->AuthorizeConnection(Admission).bAccepted));
		ASSERT_THAT(IsFalse(
			Fixture.Provider->AuthorizeConnection(Admission).bAccepted));

		Issue.PlatformIdentityType = TEXT("ProviderA");
		Issue.PlatformIdentityValue = TEXT("shared-user-value");
		Issue.IdempotencyKey = UniqueKey(TEXT("credential-typed-identity"));
		const FExecutedRequest TypedIssue = Fixture.Execute(
			ESeinOnlineOperation::IssueReconnectCredential,
			Issue,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineReconnectCredentialResult* TypedCredential =
			TypedIssue.Response.Payload.GetPtr<
				FSeinOnlineReconnectCredentialResult>();
		ASSERT_THAT(IsNotNull(TypedCredential));
		Admission.AdmissionID = TypedCredential->AdmissionID;
		Admission.PlatformIdentityType = TEXT("ProviderB");
		Admission.PlatformIdentityValue = TEXT("shared-user-value");
		ASSERT_THAT(IsFalse(
			Fixture.Provider->AuthorizeConnection(Admission).bAccepted));
		Admission.PlatformIdentityType = TEXT("ProviderA");
		ASSERT_THAT(IsTrue(
			Fixture.Provider->AuthorizeConnection(Admission).bAccepted));

		FSeinOnlineSubmitTelemetryRequest Telemetry;
		Telemetry.AccountID = Match.AccountOne;
		Telemetry.IdempotencyKey = UniqueKey(TEXT("telemetry-secret"));
		FSeinOnlineTelemetryEvent& Event =
			Telemetry.Events.AddDefaulted_GetRef();
		Event.Event = TEXT("MatchLoaded");
		Event.TimestampUnixMilliseconds = 3000;
		FSeinOnlineAttribute& Secret = Event.Attributes.AddDefaulted_GetRef();
		Secret.Key = TEXT("AccessToken");
		Secret.Value = TEXT("must-not-pass");
		const FExecutedRequest SecretTelemetry = Fixture.Execute(
			ESeinOnlineOperation::SubmitTelemetry, Telemetry);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::InvalidRequest),
			AsInt(SecretTelemetry.Response.Error.Code)));

		Telemetry.Events[0].Attributes[0].Key = TEXT("Map");
		Telemetry.Events[0].Attributes[0].Value = TEXT("Qualification");
		Telemetry.IdempotencyKey = UniqueKey(TEXT("telemetry"));
		const FExecutedRequest Accepted = Fixture.Execute(
			ESeinOnlineOperation::SubmitTelemetry, Telemetry);
		const FSeinOnlineMutationReceipt* AcceptedReceipt =
			Accepted.Response.Payload.GetPtr<FSeinOnlineMutationReceipt>();
		ASSERT_THAT(IsNotNull(AcceptedReceipt));
		const FExecutedRequest Retry = Fixture.Execute(
			ESeinOnlineOperation::SubmitTelemetry, Telemetry);
		const FSeinOnlineMutationReceipt* RetryReceipt =
			Retry.Response.Payload.GetPtr<FSeinOnlineMutationReceipt>();
		ASSERT_THAT(IsNotNull(RetryReceipt));
		ASSERT_THAT(IsTrue(
			RetryReceipt->ReceiptID == AcceptedReceipt->ReceiptID));
		Telemetry.Events[0].TimestampUnixMilliseconds++;
		const FExecutedRequest Conflict = Fixture.Execute(
			ESeinOnlineOperation::SubmitTelemetry, Telemetry);
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Conflict),
			AsInt(Conflict.Response.Error.Code)));
		Fixture.Provider->ClearUtcNowOverrideForTests();
	}

	TEST(OnlineServicesAdmissionBindsOneTimeCredentialToActiveSeat,
		"SeinARTS.Integration.OnlineServices.Admission")
	{
		FActorTestSpawner Spawner;
		Spawner.InitializeGameSubsystems();
		UGameInstance* GameInstance = Spawner.GetGameInstance();
		USeinOnlineServicesSubsystem* Online = GameInstance
			? GameInstance->GetSubsystem<USeinOnlineServicesSubsystem>()
			: nullptr;
		USeinNetSubsystem* Net = GameInstance
			? GameInstance->GetSubsystem<USeinNetSubsystem>()
			: nullptr;
		ASSERT_THAT(IsNotNull(Online));
		ASSERT_THAT(IsNotNull(Net));
		ASSERT_THAT(IsTrue(
			Online->RegisterConnectionAdmissionForTests()));
		ASSERT_THAT(IsTrue(Net->HasConnectionAdmissionAuthorizer()));

		FLoopbackFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(*Online)));
		FRegisteredMatch Match;
		ASSERT_THAT(IsTrue(BuildRegisteredMatch(
			Fixture, ESeinOnlineMatchClassification::Unranked, Match)));
		Net->SetConnectionAdmissionBindingForTests(
			Match.MatchID, Match.ParticipantOne, FSeinPlayerID(1));

		FSeinOnlineIssueReconnectCredentialRequest Issue;
		Issue.MatchID = Match.MatchID;
		Issue.ParticipantID = Match.ParticipantOne;
		Issue.IdempotencyKey = UniqueKey(TEXT("admission-credential"));
		const FExecutedRequest Issued = Fixture.Execute(
			ESeinOnlineOperation::IssueReconnectCredential,
			Issue,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineReconnectCredentialResult* Credential =
			Issued.Response.Payload.GetPtr<
				FSeinOnlineReconnectCredentialResult>();
		ASSERT_THAT(IsNotNull(Credential));

		auto MakeOptions = [](const FString& AdmissionID)
		{
			return FString::Printf(
				TEXT("?SeinAdmission=%s"), *AdmissionID);
		};

		FString Error;
		Net->SetConnectionAdmissionBindingForTests(
			FSeinMatchInstanceID(FGuid::NewGuid()),
			Match.ParticipantOne,
			FSeinPlayerID(1));
		const FString WrongMatchOptions = MakeOptions(Credential->AdmissionID);
		ASSERT_THAT(IsFalse(WrongMatchOptions.Contains(Credential->Credential)));
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			WrongMatchOptions, TEXT("127.0.0.1"),
			FUniqueNetIdRepl(), Error)));

		Net->SetConnectionAdmissionBindingForTests(
			Match.MatchID, Match.ParticipantOne, FSeinPlayerID(1));
		Issue.IdempotencyKey = UniqueKey(TEXT("admission-credential-fresh"));
		const FExecutedRequest Reissued = Fixture.Execute(
			ESeinOnlineOperation::IssueReconnectCredential,
			Issue,
			ESeinOnlineCallerAuthority::TrustedServer);
		const FSeinOnlineReconnectCredentialResult* FreshCredential =
			Reissued.Response.Payload.GetPtr<
				FSeinOnlineReconnectCredentialResult>();
		ASSERT_THAT(IsNotNull(FreshCredential));
		const FString Options = MakeOptions(FreshCredential->AdmissionID);
		ASSERT_THAT(IsFalse(Options.Contains(FreshCredential->Credential)));
		ASSERT_THAT(IsTrue(Net->AuthorizeIncomingConnection(
			Options, TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error)));
		ASSERT_THAT(IsTrue(Net->AuthorizeIncomingConnection(
			Options, TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error)));
		APlayerController& Controller =
			Spawner.SpawnActor<APlayerController>();
		FSeinPlayerID Slot;
		ASSERT_THAT(IsTrue(Net->ConsumeAuthorizedConnection(
			&Controller, Options, FUniqueNetIdRepl(), Slot, Error)));
		ASSERT_THAT(IsTrue(Slot == FSeinPlayerID(1)));
		ASSERT_THAT(IsFalse(Net->AuthorizeIncomingConnection(
			Options,
			TEXT("127.0.0.1"), FUniqueNetIdRepl(), Error)));
	}

	TEST(OnlineServicesFacadeDefersCancelsAndIgnoresStaleProviders,
		"SeinARTS.Determinism.OnlineServices.Boundary")
	{
		FActorTestSpawner Spawner;
		Spawner.InitializeGameSubsystems();
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinOnlineServicesSubsystem* Online =
			Spawner.GetGameInstance()
				->GetSubsystem<USeinOnlineServicesSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Online));

		FString Error;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			FSeinMatchSettings(),
			0x534F5342,
			TEXT("OnlineServices.Boundary"),
			&Error)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World, &Error)));
		FGuid RootBefore;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(RootBefore, Error)));

		FSeinOnlineAuthenticateRequest Authenticate;
		Authenticate.LocalUserIndex = 1301;
		Authenticate.DisplayNameHint = TEXT("Deferred");
		const FSeinOnlineRequestHandle Deferred = Online->BeginOperation(
			ESeinOnlineOperation::Authenticate,
			FInstancedStruct::Make(Authenticate),
			ESeinOnlineCallerAuthority::Client);
		ASSERT_THAT(IsTrue(Deferred.IsValid()));
		ESeinOnlineRequestStatus Status;
		FSeinOnlineError RequestError;
		FInstancedStruct Result;
		ASSERT_THAT(IsTrue(Online->GetRequestResult(
			Deferred, Status, RequestError, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Pending), AsInt(Status)));
		Online->DrainCompletionsForTests();
		ASSERT_THAT(IsTrue(Online->GetRequestResult(
			Deferred, Status, RequestError, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Succeeded), AsInt(Status)));

		const FSeinOnlineRequestHandle AlreadyCompleted = Online->BeginOperation(
			ESeinOnlineOperation::Authenticate,
			FInstancedStruct::Make(Authenticate),
			ESeinOnlineCallerAuthority::Client);
		ASSERT_THAT(IsFalse(Online->CancelRequest(AlreadyCompleted)));
		Online->DrainCompletionsForTests();
		ASSERT_THAT(IsTrue(Online->GetRequestResult(
			AlreadyCompleted, Status, RequestError, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Succeeded), AsInt(Status)));

		FSeinOnlineError ProviderError;
		ASSERT_THAT(IsTrue(Online->SetProviderClassForTests(
			USeinOnlineDeferredTestProvider::StaticClass(), ProviderError)));
		USeinOnlineDeferredTestProvider* RawDeferredProvider =
			Cast<USeinOnlineDeferredTestProvider>(
				Online->GetProviderForTests());
		ASSERT_THAT(IsNotNull(RawDeferredProvider));
		const FSeinOnlineRequestHandle Cancelled = Online->BeginOperation(
			ESeinOnlineOperation::Authenticate,
			FInstancedStruct::Make(Authenticate),
			ESeinOnlineCallerAuthority::Client);
		ASSERT_THAT(IsTrue(Online->CancelRequest(Cancelled)));
		ASSERT_THAT(IsTrue(Online->SetProviderClassForTests(
			USeinOnlineLoopbackProvider::StaticClass(), ProviderError)));
		Online->DrainCompletionsForTests();
		ASSERT_THAT(IsTrue(Online->GetRequestResult(
			Cancelled, Status, RequestError, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Cancelled), AsInt(Status)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::Cancelled),
			AsInt(RequestError.Code)));

		ASSERT_THAT(IsTrue(Online->SetProviderClassForTests(
			USeinOnlineDeferredTestProvider::StaticClass(), ProviderError)));
		RawDeferredProvider = Cast<USeinOnlineDeferredTestProvider>(
			Online->GetProviderForTests());
		ASSERT_THAT(IsNotNull(RawDeferredProvider));
		TStrongObjectPtr<USeinOnlineDeferredTestProvider> RetainedProvider(
			RawDeferredProvider);
		const FSeinOnlineRequestHandle Stale = Online->BeginOperation(
			ESeinOnlineOperation::Authenticate,
			FInstancedStruct::Make(Authenticate),
			ESeinOnlineCallerAuthority::Client);
		ASSERT_THAT(IsTrue(Online->SetProviderClassForTests(
			USeinOnlineLoopbackProvider::StaticClass(), ProviderError)));
		ASSERT_THAT(AreEqual(0, RetainedProvider->GetPendingRequestCount()));
		Online->DrainCompletionsForTests();
		ASSERT_THAT(IsTrue(Online->GetRequestResult(
			Stale, Status, RequestError, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Failed), AsInt(Status)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineErrorCode::ProviderUnavailable),
			AsInt(RequestError.Code)));

		ASSERT_THAT(IsTrue(Online->SetProviderClassForTests(
			USeinOnlineDeferredTestProvider::StaticClass(), ProviderError)));
		RawDeferredProvider = Cast<USeinOnlineDeferredTestProvider>(
			Online->GetProviderForTests());
		ASSERT_THAT(IsNotNull(RawDeferredProvider));
		FSeinOnlineAuthenticateRequest DuplicateAuthentication;
		DuplicateAuthentication.LocalUserIndex = 0;
		const FSeinOnlineRequestHandle DuplicateCallback =
			Online->Authenticate(DuplicateAuthentication);
		RawDeferredProvider->CompleteAllSuccessfullyTwice();
		Online->DrainCompletionsForTests();
		ASSERT_THAT(IsTrue(Online->ConsumeRequestResult(
			DuplicateCallback, Status, RequestError, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Succeeded), AsInt(Status)));
		ASSERT_THAT(IsFalse(Online->ConsumeRequestResult(
			DuplicateCallback, Status, RequestError, Result)));

		const FSeinOnlineRequestHandle ThreadedCallback =
			Online->Authenticate(DuplicateAuthentication);
		RawDeferredProvider->CompleteAllSuccessfullyOnWorkerThread();
		RawDeferredProvider->WaitForWorkerTasks();
		Online->DrainCompletionsForTests();
		ASSERT_THAT(IsTrue(Online->ConsumeRequestResult(
			ThreadedCallback, Status, RequestError, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Succeeded), AsInt(Status)));

		FGuid RootAfter;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(RootAfter, Error)));
		ASSERT_THAT(IsTrue(RootBefore == RootAfter));
		World->StopSimulation();
	}

	TEST(OnlineServicesModuleReleaseJoinsProviderCallbacks,
		"SeinARTS.Integration.OnlineServices.Lifecycle")
	{
		FActorTestSpawner Spawner;
		Spawner.InitializeGameSubsystems();
		USeinOnlineServicesSubsystem* Online = Spawner.GetGameInstance()
			? Spawner.GetGameInstance()->GetSubsystem<
				USeinOnlineServicesSubsystem>()
			: nullptr;
		ASSERT_THAT(IsNotNull(Online));
		FSeinOnlineError ProviderError;
		ASSERT_THAT(IsTrue(Online->SetProviderClassForTests(
			USeinOnlineDeferredTestProvider::StaticClass(), ProviderError)));
		USeinOnlineDeferredTestProvider* DeferredProvider =
			Cast<USeinOnlineDeferredTestProvider>(Online->GetProviderForTests());
		ASSERT_THAT(IsNotNull(DeferredProvider));
		TStrongObjectPtr<USeinOnlineDeferredTestProvider> RetainedProvider(
			DeferredProvider);

		FSeinOnlineAuthenticateRequest Request;
		Request.LocalUserIndex = 1441;
		ASSERT_THAT(IsTrue(Online->Authenticate(Request).IsValid()));
		DeferredProvider->CompleteAllSuccessfullyOnWorkerThread();
		Online->ReleaseModuleOwnedStateForModuleUnload();
		ASSERT_THAT(AreEqual(0, RetainedProvider->GetPendingRequestCount()));
		ASSERT_THAT(IsFalse(Online->IsReady()));
	}

	TEST(OnlineServicesFacadeEvictsCompletedRecordsAtTotalCapacity,
		"SeinARTS.Unit.OnlineServices.Capacity")
	{
		FActorTestSpawner Spawner;
		Spawner.InitializeGameSubsystems();
		USeinOnlineServicesSubsystem* Online =
			Spawner.GetGameInstance()
				->GetSubsystem<USeinOnlineServicesSubsystem>();
		ASSERT_THAT(IsNotNull(Online));
		Online->SetRequestLimitsForTests(16, 16);

		TArray<FSeinOnlineRequestHandle> Handles;
		for (int32 Index = 0; Index < 17; ++Index)
		{
			FSeinOnlineAuthenticateRequest Request;
			Request.LocalUserIndex = 2000 + Index;
			const FSeinOnlineRequestHandle Handle = Online->Authenticate(Request);
			ASSERT_THAT(IsTrue(Handle.IsValid()));
			Handles.Add(Handle);
			Online->DrainCompletionsForTests();
		}

		ESeinOnlineRequestStatus Status;
		FSeinOnlineError Error;
		FInstancedStruct Result;
		ASSERT_THAT(IsFalse(Online->GetRequestResult(
			Handles[0], Status, Error, Result)));
		ASSERT_THAT(IsTrue(Online->GetRequestResult(
			Handles.Last(), Status, Error, Result)));
		ASSERT_THAT(AreEqual(
			AsInt(ESeinOnlineRequestStatus::Succeeded), AsInt(Status)));
	}
}
