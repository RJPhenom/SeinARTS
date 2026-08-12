#include "CQTest.h"
#include "SeinNetSubsystem.h"
#include "SeinNetCommandWireCodec.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "Input/SeinCommandSchemaRegistry.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinCommandSchemaTestTypes.h"

struct FSeinNetSubsystemTestAccess
{
	static FSeinNetworkParticipantID Participant(uint32 Suffix = 1)
	{
		return FSeinNetworkParticipantID(FGuid(
			0x12345678, 0x23456789, 0x3456789A, Suffix));
	}

	static FSeinParticipantBinding Binding(uint32 Suffix = 1, uint8 Slot = 1)
	{
		FSeinParticipantBinding Result;
		Result.ParticipantID = Participant(Suffix);
		Result.CommandSlots.Add(FSeinPlayerID(Slot));
		Result.bSimulates = true;
		Result.bReportsWorldRoots = true;
		Result.bCanCoordinate = Suffix == 1;
		Result.bCanAdministerMatch = Suffix == 1;
		return Result;
	}

	static FSeinProtocolContext ContextFor(
		const TArray<FSeinParticipantBinding>& Bindings,
		int64 Epoch = 1)
	{
		return FSeinProtocolContext(
			FSeinMatchInstanceID(FGuid(1, 2, 3, 4)),
			Epoch,
			Participant(),
			1,
			1,
			SeinComputeMembershipDigest(Bindings),
			FGuid(13, 14, 15, 16),
			FGuid(5, 6, 7, 8),
			FGuid(17, 18, 19, 20),
			FGuid(9, 10, 11, 12));
	}

	static void SeedConfiguredProtocol(
		USeinNetSubsystem& Net,
		int32 AuthorCount = 1,
		int32 MaxCommandsPerSubmission = 256)
	{
		AuthorCount = FMath::Clamp(AuthorCount, 1, 16);
		Net.ParticipantBindings.Reset();
		Net.SlotToParticipant.Reset();
		Net.SlotLifecycle.Reset();
		Net.ActiveMatchSettings.Slots.Reset();
		for (int32 Index = 1; Index <= AuthorCount; ++Index)
		{
			const FSeinParticipantBinding AuthorBinding = Binding(Index, Index);
			Net.ParticipantBindings.Add(AuthorBinding);
			Net.SlotToParticipant.Add(
				FSeinPlayerID(Index), AuthorBinding.ParticipantID);
			FSeinMatchSlot& MatchSlot = Net.ActiveMatchSettings.Slots.Emplace_GetRef();
			MatchSlot.SlotIndex = Index;
			MatchSlot.State = ESeinSlotState::Human;
			Net.SlotLifecycle.Add(
				FSeinPlayerID(Index), ESeinSlotLifecycle::Connected);
		}
		Net.CoordinatorParticipantID = Participant();
		Net.ActiveProtocolContext = ContextFor(Net.ParticipantBindings);
		Net.TurnAggregator.Configure(Net.ActiveProtocolContext, Net.ParticipantBindings);
		Net.bHasActiveMatchSettings = true;
		Net.FrozenMaxCommandsPerSubmission = FMath::Clamp(
			MaxCommandsPerSubmission, 1,
			SeinNetProtocolLimits::MaxCommandsPerAuthor);
	}

	static USeinNetSubsystem* NewSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		USeinNetSubsystem* Net = NewObject<USeinNetSubsystem>(GameInstance);
		Net->TestFindCommandSchemaOverride = [](
			FGameplayTag CommandType,
			int32 SchemaVersion,
			FSeinCommandSchemaDescriptor& OutSchema)
		{
			OutSchema = {};
			OutSchema.CommandType = CommandType;
			OutSchema.SchemaVersion = SchemaVersion;
			OutSchema.MaxPayloadBytes = 4096;
			OutSchema.MaxPayloadAggregateElements = 256;
			if (CommandType == SeinARTSTags::Command_Type_EndMatch)
			{
				OutSchema.AuthorityScope = ESeinCommandAuthorityScope::MatchControl;
				return true;
			}
			if (CommandType == SeinARTSTags::Command_Type_Ping)
			{
				OutSchema.AuthorityScope = ESeinCommandAuthorityScope::PublicObserver;
				OutSchema.MaxEntityListEntries = 100000;
				if (SchemaVersion == 2)
				{
					OutSchema.PayloadStruct =
						FSeinCommandSchemaLargeWireArrayPayload::StaticStruct();
					OutSchema.MaxPayloadBytes = 2 * 1024 * 1024;
					OutSchema.MaxPayloadAggregateElements = 512;
				}
				return true;
			}
			if (CommandType == SeinARTSTags::Command_Type_ResumeMatchRequest)
			{
				OutSchema.AuthorityScope = ESeinCommandAuthorityScope::MatchControl;
				OutSchema.AllowedExecutionContexts = static_cast<int32>(
					ESeinCommandExecutionAllowance::FrozenPauseControl);
				return true;
			}
			return false;
		};
		return Net;
	}

	static int32 InputDelay(const USeinNetSubsystem& Net) { return Net.GetInputDelayTurns(); }
	static int32 TicksPerTurn(const USeinNetSubsystem& Net) { return Net.GetTicksPerTurn(); }
	static void Buffer(USeinNetSubsystem& Net, const FSeinCommand& Command)
	{
		check(Net.TryBufferOutgoingDraft(FSeinCommandSubmissionDraft(Command, false)));
	}
	static bool TryBuffer(USeinNetSubsystem& Net, const FSeinCommand& Command)
	{
		return Net.TryBufferOutgoingDraft(
			FSeinCommandSubmissionDraft(Command, false));
	}
	static void QueueThrough(USeinNetSubsystem& Net, int32 Turn, bool bAttachCommands)
	{
		Net.QueueTurnSubmissionsThrough(Turn, bAttachCommands);
	}
	static void FlushTurns(USeinNetSubsystem& Net) { Net.FlushPendingTurnSubmissions(); }
	static int32 PendingTurnCount(const USeinNetSubsystem& Net) { return Net.PendingTurnSubmissions.Num(); }
	static int32 PendingCommandCount(const USeinNetSubsystem& Net) { return Net.PendingOutgoingDrafts.Num(); }
	static FSeinPlayerID PendingDerivedPayer(const USeinNetSubsystem& Net)
	{
		return Net.PendingOutgoingDrafts.Drafts[0].Command.DerivedResourcePayer;
	}
	static void SetNetworkingActive(USeinNetSubsystem& Net, bool bActive)
	{
		Net.TestNetworkingActiveOverride = bActive;
	}
	static int32 FrozenCommandTick(const USeinNetSubsystem& Net)
	{
		return Net.PendingTurnSubmissions[0].Drafts[0].Command.Tick;
	}
	static int32 FrozenDraftCount(const USeinNetSubsystem& Net, int32 PendingIndex)
	{
		return Net.PendingTurnSubmissions[PendingIndex].Drafts.Num();
	}
	static int32 FrozenDraftTick(
		const USeinNetSubsystem& Net, int32 PendingIndex, int32 DraftIndex)
	{
		return Net.PendingTurnSubmissions[PendingIndex].Drafts[DraftIndex].Command.Tick;
	}
	static int32 FrozenMaxCommands(const USeinNetSubsystem& Net)
	{
		return Net.FrozenMaxCommandsPerSubmission;
	}
	static int32 LastSubmittedTurn(const USeinNetSubsystem& Net) { return Net.LastSubmittedTurn; }

	static void SetTurnTransport(USeinNetSubsystem& Net,
		TFunction<bool(int32, const TArray<FSeinCommand>&)> Transport)
	{
		Net.TestTurnSubmitOverride = MoveTemp(Transport);
	}

	static void EnqueueWorldRoot(
		USeinNetSubsystem& Net,
		int32 Turn,
		const FGuid& WorldRoot)
	{
		Net.EnqueueWorldStateRootReport(Turn, WorldRoot);
	}
	static void FlushWorldRoots(USeinNetSubsystem& Net)
	{
		Net.FlushPendingWorldStateRootReports();
	}
	static int32 PendingWorldRootCount(const USeinNetSubsystem& Net)
	{
		return Net.PendingWorldStateRootReports.Num();
	}
	static int32 LastWorldRootReported(const USeinNetSubsystem& Net)
	{
		return Net.LastWorldStateRootReportedTurn;
	}
	static void SetWorldRootTransport(
		USeinNetSubsystem& Net,
		TFunction<bool(int32, const FGuid&)> Transport)
	{
		Net.TestWorldStateRootSubmitOverride = MoveTemp(Transport);
	}
	static void SetDeterminismFailureTransport(
		USeinNetSubsystem& Net,
		TFunction<bool(const FSeinDeterminismSessionFailure&)> Transport)
	{
		Net.TestDeterminismSessionFailureSubmitOverride =
			MoveTemp(Transport);
	}
	static void SetCustomDeterminismFailureSubmitter(
		USeinNetSubsystem& Net,
		TFunction<bool(
			const FSeinProtocolContext&,
			const FSeinDeterminismSessionFailure&)> Submit)
	{
		FSeinDeterminismSessionFailureSubmitter Submitter;
		Submitter.BindLambda(
			[Submit = MoveTemp(Submit)](
				const FSeinProtocolContext& Context,
				const FSeinDeterminismSessionFailure& Failure) mutable
			{
				return Submit(Context, Failure);
			});
		Net.SetDeterminismSessionFailureSubmitter(
			MoveTemp(Submitter));
	}
	static void SetWorldRootResolver(
		USeinNetSubsystem& Net,
		TFunction<bool(FGuid&, FString&)> Resolver)
	{
		Net.TestWorldStateRootResolverOverride = MoveTemp(Resolver);
	}
	static bool ValidateParticipantManifestAssignment(
		const USeinNetSubsystem& Net,
		const FSeinProtocolContext& Context,
		FSeinPlayerID LocalSlot,
		FSeinNetworkParticipantID LocalParticipantID,
		bool bLocalSimulates,
		const TArray<FSeinParticipantBinding>& Bindings,
		const FSeinMatchSettings& MatchSettings,
		TArray<FSeinParticipantBinding>& OutCanonicalBindings,
		TMap<FSeinPlayerID, FSeinNetworkParticipantID>& OutSlotMap,
		FString& OutError)
	{
		return Net.ValidateParticipantManifestAssignment(
			Context,
			LocalSlot,
			LocalParticipantID,
			bLocalSimulates,
			Bindings,
			MatchSettings,
			OutCanonicalBindings,
			OutSlotMap,
			OutError);
	}
	static void SetDeterminismSchedule(
		USeinNetSubsystem& Net,
		bool bEnabled,
		int32 Interval)
	{
		Net.TestDeterminismGossipEnabledOverride = bEnabled;
		Net.TestDeterminismCheckIntervalOverride = Interval;
	}
	static void SetDeterminismGossipEnabledWithoutScheduleOverride(
		USeinNetSubsystem& Net)
	{
		Net.TestDeterminismGossipEnabledOverride = true;
		Net.TestDeterminismCheckIntervalOverride.Reset();
	}
	static void SetCurrentTurn(USeinNetSubsystem& Net, int32 Turn)
	{
		Net.TestCurrentTurnOverride = Turn;
	}
	static void SetLocalParticipant(
		USeinNetSubsystem& Net,
		FSeinNetworkParticipantID ParticipantID)
	{
		Net.LocalParticipantID = ParticipantID;
	}
	static void MaybeSubmitWorldRoot(USeinNetSubsystem& Net, int32 Turn)
	{
		Net.MaybeSubmitWorldStateRootCheck(Turn);
	}
	static bool HasPendingDeterminismFailure(const USeinNetSubsystem& Net)
	{
		return Net.PendingDeterminismSessionFailureReport.IsSet();
	}
	static void FlushDeterminismFailures(USeinNetSubsystem& Net)
	{
		Net.FlushPendingDeterminismSessionFailure();
	}
	static bool RetryDeterminismFailure(USeinNetSubsystem& Net)
	{
		return Net.RetryPendingDeterminismSessionFailureReport();
	}
	static void ReportLocalTopologyFailure(
		USeinNetSubsystem& Net,
		const FString& Reason = TEXT("forced test topology invalidation"))
	{
		Net.HandleExecutionTopologyInvalidated(Reason);
	}
	static void ApplyDueDeterminismFailures(
		USeinNetSubsystem& Net,
		int32 ThroughTurn)
	{
		Net.ApplyDueAuthenticatedDeterminismSessionFailuresThrough(
			ThroughTurn);
	}
	static const FSeinDeterminismSessionFailure& DeterminismFailure(
		const USeinNetSubsystem& Net)
	{
		return Net.DeterminismSessionFailure;
	}
	static bool IsDeterminismFailureAuthoritative(
		const USeinNetSubsystem& Net)
	{
		return Net.IsDeterminismSessionFailureAuthoritative();
	}
	static bool IsTurnReady(USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.ResolveTurnReady(Turn);
	}
	static void SetServer(USeinNetSubsystem& Net, bool bServer)
	{
		Net.TestServerOverride = bServer;
	}
	static void Prune(USeinNetSubsystem& Net, int32 ReferenceTurn)
	{
		Net.PruneProtocolState(ReferenceTurn);
	}

	static void SeedEpochState(USeinNetSubsystem& Net)
	{
		SeedConfiguredProtocol(Net);
		const FSeinTurnAuthor Author(Participant(), FSeinPlayerID(1));
		Net.TurnAggregator.Submit(Net.ActiveProtocolContext, 7, Author, {});
		Net.TurnAggregator.Submit(Net.ActiveProtocolContext, 6, Author, {});
		TArray<FSeinCommand> Assembled;
		Net.TurnAggregator.TryCommit(Net.ActiveProtocolContext, 6, Assembled);
		Net.TurnAggregator.PruneThroughTurn(2);
		Net.ReceivedTurns.FindOrAdd(8);
		Net.PendingOutgoingDrafts.Drafts.AddDefaulted();
		FSeinPendingTurnSubmission& Submission = Net.PendingTurnSubmissions.Emplace_GetRef();
		Submission.TurnId = 9;
		Net.LastQueuedTurn = 9;
		Net.LastSubmittedTurn = 8;
		Net.bStartSessionRequested = true;
		Net.bServerStartRequested = true;

		Net.ServerWorldStateRootReports.FindOrAdd(10);
		Net.CompletedWorldStateRootChecks.Add(5);
		Net.CompletedWorldStateRootRejectionFloor = 1;
		Net.LatestSuccessfulWorldStateRootCheckTurn = 5;
		Net.LatestSuccessfulWorldStateRootReporterCount = 2;
		Net.PendingWorldStateRootReports.Add(
			{11, FGuid(1, 2, 3, 4)});
		Net.LastWorldStateRootQueuedTurn = 11;
		Net.LastWorldStateRootReportedTurn = 10;
		Net.DeterminismSessionFailure.Kind =
			ESeinDeterminismSessionFailureKind::
				CanonicalRootCheckpointExpired;
		Net.DeterminismSessionFailure.Turn = 10;
		Net.DeterminismSessionFailure.ParticipantID = Participant();
		Net.bDeterminismSessionFailureAuthoritative = true;
		Net.PendingDeterminismSessionFailureReport =
			Net.DeterminismSessionFailure;
		FSeinDeterminismSessionFailure PendingTopologyFailure;
		PendingTopologyFailure.Kind =
			ESeinDeterminismSessionFailureKind::
				ExecutionTopologyInvalidated;
		PendingTopologyFailure.Turn = 20;
		PendingTopologyFailure.ParticipantID = Participant();
		Net.PendingAuthenticatedDeterminismSessionFailures
			.FindOrAdd(20)
			.Add(PendingTopologyFailure);
		Net.PendingAICommands.FindOrAdd(FSeinPlayerID(1)).Commands.AddDefaulted();
		Net.IncompleteTurnDiagnostics.Add(7, {1.0, 1.0, false});

		// Deliberately match-scoped: the epoch reset must not guess these
		// seamless-travel semantics before Gate A.
		Net.SessionSeed = 77;
		Net.bDesyncDetected = true;
		Net.SlotLifecycle.Add(FSeinPlayerID(1), ESeinSlotLifecycle::Connected);
	}

	static void ResetEpoch(USeinNetSubsystem& Net) { Net.ResetLockstepEpochState(nullptr); }
	static void ResetMatch(USeinNetSubsystem& Net) { Net.ResetMatchState(nullptr); }
	static USeinWorldSubsystem* InstallSyntheticWorldHooks(
		USeinNetSubsystem& Net,
		int32& TickCallbackCount,
		int32& TopologyCallbackCount)
	{
		UWorld* World = NewObject<UWorld>();
		USeinWorldSubsystem* WorldSub =
			NewObject<USeinWorldSubsystem>(World);
		Net.CachedWorldSub = WorldSub;
		Net.TickCompletedHandle =
			WorldSub->OnSimTickCompleted.AddLambda(
				[&TickCallbackCount](int32) { ++TickCallbackCount; });
		Net.ExecutionTopologyInvalidatedHandle =
			WorldSub->OnExecutionTopologyInvalidated.AddLambda(
				[&TopologyCallbackCount](const FString&)
				{
					++TopologyCallbackCount;
				});
		return WorldSub;
	}
	static bool AreWorldHookHandlesClear(
		const USeinNetSubsystem& Net)
	{
		return !Net.TickCompletedHandle.IsValid()
			&& !Net.ExecutionTopologyInvalidatedHandle.IsValid()
			&& !Net.CachedWorldSub.IsValid();
	}
	static bool IsEpochStateClear(const USeinNetSubsystem& Net)
	{
		return Net.TurnAggregator.GetPendingTurnIDs().IsEmpty() &&
			Net.TurnAggregator.GetRetainedCommittedTurnCount() == 0 &&
			Net.TurnAggregator.GetTurnRejectionFloor() == -1 && Net.ReceivedTurns.IsEmpty() &&
			Net.PendingOutgoingDrafts.IsEmpty() && Net.PendingTurnSubmissions.IsEmpty() &&
			Net.LastQueuedTurn == -1 && Net.LastSubmittedTurn == -1 && !Net.bStartSessionRequested &&
			!Net.bServerStartRequested &&
			Net.ServerWorldStateRootReports.IsEmpty()
			&& Net.CompletedWorldStateRootChecks.IsEmpty()
			&& Net.CompletedWorldStateRootRejectionFloor == -1
			&& Net.LatestSuccessfulWorldStateRootCheckTurn == -1
			&& Net.LatestSuccessfulWorldStateRootReporterCount == 0
			&& Net.PendingWorldStateRootReports.IsEmpty()
			&& Net.LastWorldStateRootQueuedTurn == -1
			&& Net.LastWorldStateRootReportedTurn == -1
			&& !Net.DeterminismSessionFailure.IsValid()
			&& !Net.bDeterminismSessionFailureAuthoritative
			&& !Net.PendingDeterminismSessionFailureReport.IsSet()
			&& Net.PendingAuthenticatedDeterminismSessionFailures.IsEmpty() &&
			Net.PendingAICommands.IsEmpty() && Net.AITakeoverControllers.IsEmpty() &&
			Net.IncompleteTurnDiagnostics.IsEmpty();
	}
	static bool MatchStateWasPreserved(const USeinNetSubsystem& Net)
	{
		return Net.SessionSeed == 77 && Net.bDesyncDetected &&
			Net.SlotLifecycle.Contains(FSeinPlayerID(1)) &&
			Net.ActiveProtocolContext.IsValid() && Net.ParticipantBindings.Num() == 1;
	}
	static bool AcceptsCommandTurn(const USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.IsCommandTurnWithinProtocolWindow(Turn, TEXT("Test"));
	}
	static void SeedAndPruneCompletedHistory(USeinNetSubsystem& Net, int32 ReferenceTurn)
	{
		SeedConfiguredProtocol(Net);
		const FSeinTurnAuthor Author(Participant(), FSeinPlayerID(1));
		for (int32 Turn = 0; Turn <= ReferenceTurn; ++Turn)
		{
			Net.TurnAggregator.Submit(Net.ActiveProtocolContext, Turn, Author, {});
			TArray<FSeinCommand> Assembled;
			Net.TurnAggregator.TryCommit(Net.ActiveProtocolContext, Turn, Assembled);
			Net.CompletedWorldStateRootChecks.Add(Turn);
		}
		Net.PruneProtocolState(ReferenceTurn);
	}
	static int32 CompletedTurnCount(const USeinNetSubsystem& Net)
	{
		return Net.TurnAggregator.GetRetainedCommittedTurnCount();
	}
	static int32 CompletedWorldRootCount(const USeinNetSubsystem& Net)
	{
		return Net.CompletedWorldStateRootChecks.Num();
	}
	static int32 TurnFloor(const USeinNetSubsystem& Net) { return Net.TurnAggregator.GetTurnRejectionFloor(); }
	static int32 WorldRootFloor(const USeinNetSubsystem& Net)
	{
		return Net.CompletedWorldStateRootRejectionFloor;
	}

	static void SetDedicatedAuthority(USeinNetSubsystem& Net, bool bDedicated)
	{
		Net.TestDedicatedAuthorityOverride = bDedicated;
	}
	static void SetSessionSeed(USeinNetSubsystem& Net, int64 Seed)
	{
		SeedConfiguredProtocol(Net);
		Net.SessionSeed = Seed;
		Net.LocalParticipantID = Participant();
	}
	static bool StartPrerequisitesReady(const USeinNetSubsystem& Net, bool bHooksReady)
	{
		return Net.AreNetworkStartPrerequisitesReady(bHooksReady);
	}
	static void BufferDedicatedAuthorityTurn(
		USeinNetSubsystem& Net, int32 Turn, const TArray<FSeinCommand>& Commands)
	{
		Net.BufferAssembledTurnForDedicatedAuthority(Turn, Commands);
	}
	static bool HasReceivedTurn(const USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.ReceivedTurns.Contains(Turn);
	}
	static int32 ReceivedCommandTick(const USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.ReceivedTurns.FindChecked(Turn)[0].Tick;
	}

	static int32 BufferWorldRootFirstWins(
		USeinNetSubsystem& Net,
		int32 Turn,
		FSeinNetworkParticipantID ParticipantID,
		const FGuid& WorldRoot)
	{
		return static_cast<int32>(
			Net.BufferWorldStateRootReportFirstWins(
				Turn, ParticipantID, WorldRoot));
	}
	static FGuid StoredWorldRoot(
		const USeinNetSubsystem& Net,
		int32 Turn,
		FSeinNetworkParticipantID ParticipantID)
	{
		return Net.ServerWorldStateRootReports.FindChecked(Turn)
			.FindChecked(ParticipantID);
	}
	static void SetLifecycle(USeinNetSubsystem& Net, FSeinPlayerID Slot, ESeinSlotLifecycle Lifecycle)
	{
		Net.SlotLifecycle.Add(Slot, Lifecycle);
	}
	static bool CommandLifecycleAllowed(const USeinNetSubsystem& Net, FSeinPlayerID Slot)
	{
		return Net.IsCommandSubmissionLifecycleAllowed(Slot);
	}
	static bool ConfigFingerprintsComplete(
		const TArray<FSeinNetworkParticipantID>& Expected,
		const TMap<FSeinNetworkParticipantID, int32>& Accepted,
		int32 RequiredFingerprint)
	{
		return USeinNetSubsystem::AreConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint);
	}
	static void BufferAICommand(
		USeinNetSubsystem& Net, FSeinPlayerID Slot, const FSeinCommand& Command)
	{
		check(Net.TryBufferAICommand(Slot, Command));
	}
	static bool TryBufferAICommand(
		USeinNetSubsystem& Net, FSeinPlayerID Slot, const FSeinCommand& Command)
	{
		return Net.TryBufferAICommand(Slot, Command);
	}
	static void ConsumeAICommands(
		USeinNetSubsystem& Net, FSeinPlayerID Slot, int32 Count)
	{
		Net.ConsumeAICommandPrefix(Slot, Count);
	}
	static bool BuildDroppedSubmission(
		USeinNetSubsystem& Net, FSeinPlayerID Slot, int32 Turn,
		bool bAllowAICommands, TArray<FSeinCommand>& OutCommands)
	{
		return Net.BuildDroppedSlotSubmission(
			Slot, Turn, bAllowAICommands, OutCommands);
	}
	static int32 PendingAICommandCount(const USeinNetSubsystem& Net, FSeinPlayerID Slot)
	{
		const FSeinAICommandBacklog* Pending = Net.PendingAICommands.Find(Slot);
		return Pending ? Pending->Num() : 0;
	}
	static int32 PendingAIBacklogCount(const USeinNetSubsystem& Net)
	{
		return Net.PendingAICommands.Num();
	}
	static void StampBatch(
		const USeinNetSubsystem& Net,
		TArray<FSeinCommand>& Commands,
		FSeinPlayerID Slot,
		bool bParticipantCanAdministerMatch,
		int32 Turn)
	{
		Net.StampAuthoritativeCommandBatch(
			Commands, Slot, bParticipantCanAdministerMatch, Turn);
	}
	static void MarkTurnIncomplete(
		USeinNetSubsystem& Net, int32 Turn, bool bReachedExecutionGate = false)
	{
		Net.IncompleteTurnDiagnostics.Add(
			Turn, {1.0, 1.0, false, bReachedExecutionGate});
	}
	static void FinalizeTurnDiagnostics(
		USeinNetSubsystem& Net, int32 Turn, FSeinPlayerID CompletingSubmitter)
	{
		Net.FinalizeCompletedTurnDiagnostics(Turn, CompletingSubmitter);
	}
	static bool ResolveTurnReady(USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.ResolveTurnReady(Turn);
	}
	static bool HasIncompleteTurn(const USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.IncompleteTurnDiagnostics.Contains(Turn);
	}
	static int32 StragglerCount(const USeinNetSubsystem& Net, FSeinPlayerID Slot)
	{
		return Net.StragglerCounts.FindRef(Slot);
	}

	static void ConfigureTravelTest(USeinNetSubsystem& Net)
	{
		SeedConfiguredProtocol(Net);
		Net.TestServerOverride = true;
		Net.TestDedicatedAuthorityOverride = true;
		Net.TestCommandProtocolDigestOverride = FGuid(9, 10, 11, 12);
		Net.TestSimulationContentDigestOverride = FGuid(17, 18, 19, 20);
		Net.TestCommandProtocolMaxCommandsOverride = 256;
		Net.TestParticipantManifestOverride = TArray<FSeinParticipantBinding>{Binding()};
	}
	static bool PrepareTravel(USeinNetSubsystem& Net, ESeinMatchTravelIntent Intent)
	{
		return Net.PrepareMatchTravel(
			Intent,
			FName(TEXT("/Game/Tests/L_SeinAutomation_Network")),
			ESeinPreparedWorldActivation::AllowCurrentWorld);
	}
	static void ConsumePreparedDestination(USeinNetSubsystem& Net)
	{
		Net.RetireReplayEpochForCommittedTravel();
		Net.CancelPendingProtocolPromotion();
		Net.PendingLocalProtocolAssignment.Reset();
		FSeinPendingAuthorityProtocolState Prepared =
			MoveTemp(Net.PendingAuthorityProtocolState);
		Net.PendingAuthorityProtocolState.Reset();
		Net.ActiveProtocolContext = Prepared.Context;
		Net.ActiveMatchSettings = MoveTemp(Prepared.MatchSettings);
		Net.bHasActiveMatchSettings = true;
		Net.SessionSeed = Prepared.Seed;
		Net.ParticipantBindings = MoveTemp(Prepared.ParticipantBindings);
		Net.SlotToParticipant = MoveTemp(Prepared.SlotToParticipant);
		Net.CoordinatorParticipantID = Prepared.CoordinatorParticipantID;
		Net.SlotLifecycle = MoveTemp(Prepared.SlotLifecycle);
		Net.FrozenMaxCommandsPerSubmission =
			Prepared.FrozenMaxCommandsPerSubmission;
		Net.bDestinationStartPending = false;
	}
	static bool IsDestinationPending(const USeinNetSubsystem& Net)
	{
		return Net.bDestinationStartPending;
	}
	static void InstallReplayObjects(USeinNetSubsystem& Net)
	{
		Net.ReplayWriter = NewObject<USeinReplayWriter>(&Net);
		Net.ReplayReader = NewObject<USeinReplayReader>(&Net);
	}
	static bool HasReplayObjects(const USeinNetSubsystem& Net)
	{
		return Net.ReplayWriter != nullptr && Net.ReplayReader != nullptr;
	}
	static int64 SessionSeed(const USeinNetSubsystem& Net) { return Net.SessionSeed; }
	static int64 PreparedSeed(const USeinNetSubsystem& Net)
	{
		return Net.PendingAuthorityProtocolState.Seed;
	}
	static const FSeinProtocolContext& Context(const USeinNetSubsystem& Net)
	{
		return Net.ActiveProtocolContext;
	}
	static const FSeinProtocolContext& PreparedContext(
		const USeinNetSubsystem& Net)
	{
		return Net.PendingAuthorityProtocolState.Context;
	}
	static bool ActivationEligible(
		const UWorld* CurrentWorld,
		const UWorld* SourceWorld,
		ESeinPreparedWorldActivation Activation,
		const FGuid& LoadedDigest,
		const FGuid& DestinationDigest)
	{
		return USeinNetSubsystem::IsPreparedWorldActivationEligible(
			CurrentWorld,
			SourceWorld,
			Activation,
			LoadedDigest,
			DestinationDigest);
	}
	static void DeliverTurn(
		USeinNetSubsystem& Net,
		const FSeinProtocolContext& Context,
		int32 Turn,
		const TArray<FSeinCommand>& Commands)
	{
		FSeinOpaqueCommandBatch Opaque;
		FString Error;
		const bool bEncoded = FSeinNetCommandWireCodec::EncodeCommands(
			Commands,
			SeinNetProtocolLimits::MaxCommandsPerCanonicalTurn,
			[&Net](FGameplayTag Type, int32 Version, FSeinCommandSchemaDescriptor& Out)
			{
				return Net.FindFrozenCommandSchema(Type, Version, Out);
			},
			Opaque,
			Error);
		checkf(bEncoded, TEXT("test turn wire encode failed: %s"), *Error);
		Net.ClientHandleTurn(Context, Turn, Opaque);
	}

	static FSeinCommand Ping(int32 Marker = 0)
	{
		FSeinCommand Command;
		Command.CommandType = SeinARTSTags::Command_Type_Ping;
		Command.SchemaVersion = 1;
		Command.Tick = Marker;
		return Command;
	}

	static FSeinCommand WidePing(int32 EntityCount, int32 Marker = 0)
	{
		FSeinCommand Command = Ping(Marker);
		Command.EntityList.SetNum(EntityCount);
		return Command;
	}

	static FSeinCommand ExpandedPayloadPing(int32 ElementCount, int32 Marker = 0)
	{
		FSeinCommand Command = Ping(Marker);
		Command.SchemaVersion = 2;
		FSeinCommandSchemaLargeWireArrayPayload Payload;
		Payload.Values.SetNum(ElementCount);
		Command.Payload.InitializeAs<FSeinCommandSchemaLargeWireArrayPayload>(Payload);
		return Command;
	}
};

namespace UE::SeinARTSTests
{
	TEST(FrozenTurnSubmissionRetriesExactBatch, "SeinARTS.Unit.Network")
	{
		TestRunner->AddExpectedError(
			TEXT("FlushPendingTurnSubmissions: retaining turn="),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);

		FSeinCommand First;
		First.CommandType = SeinARTSTags::Command_Type_Ping;
		First.Tick = 111;
		FSeinCommand Later;
		Later.CommandType = SeinARTSTags::Command_Type_Ping;
		Later.Tick = 222;
		const int32 FirstTurn = FSeinNetSubsystemTestAccess::InputDelay(*Net);

		FSeinNetSubsystemTestAccess::Buffer(*Net, First);
		FSeinNetSubsystemTestAccess::QueueThrough(*Net, FirstTurn, true);
		FSeinNetSubsystemTestAccess::SetTurnTransport(*Net,
			[](int32, const TArray<FSeinCommand>&) { return false; });
		FSeinNetSubsystemTestAccess::FlushTurns(*Net);

		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::PendingTurnCount(*Net)));
		ASSERT_THAT(AreEqual(111, FSeinNetSubsystemTestAccess::FrozenCommandTick(*Net)));
		ASSERT_THAT(AreEqual(-1, FSeinNetSubsystemTestAccess::LastSubmittedTurn(*Net)));

		// Input issued during the stall remains for the next boundary; it must
		// not be merged into the already-frozen failed turn.
		FSeinNetSubsystemTestAccess::Buffer(*Net, Later);
		int32 RetriedTurn = INDEX_NONE;
		int32 RetriedTick = INDEX_NONE;
		FSeinNetSubsystemTestAccess::SetTurnTransport(*Net,
			[&](int32 Turn, const TArray<FSeinCommand>& Commands)
			{
				RetriedTurn = Turn;
				RetriedTick = Commands.IsEmpty() ? INDEX_NONE : Commands[0].Tick;
				return true;
			});
		FSeinNetSubsystemTestAccess::FlushTurns(*Net);

		ASSERT_THAT(AreEqual(FirstTurn, RetriedTurn));
		ASSERT_THAT(AreEqual(111, RetriedTick));
		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::PendingTurnCount(*Net)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::PendingCommandCount(*Net)));
		ASSERT_THAT(AreEqual(FirstTurn, FSeinNetSubsystemTestAccess::LastSubmittedTurn(*Net)));

		int32 NextTick = INDEX_NONE;
		FSeinNetSubsystemTestAccess::QueueThrough(*Net, FirstTurn + 1, true);
		FSeinNetSubsystemTestAccess::SetTurnTransport(*Net,
			[&](int32, const TArray<FSeinCommand>& Commands)
			{
				NextTick = Commands.IsEmpty() ? INDEX_NONE : Commands[0].Tick;
				return true;
			});
		FSeinNetSubsystemTestAccess::FlushTurns(*Net);
		ASSERT_THAT(AreEqual(222, NextTick));
	}

	TEST(LocalBurstFreezesLargestCountPrefixOnlyAtRealBoundaries,
		"SeinARTS.Unit.Network.Protocol")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(
			*Net, /*AuthorCount=*/1, /*MaxCommandsPerSubmission=*/2);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
			*Net, FSeinNetSubsystemTestAccess::Ping(1))));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
			*Net, FSeinNetSubsystemTestAccess::Ping(2))));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
			*Net, FSeinNetSubsystemTestAccess::Ping(3))));

		const int32 FirstTurn = FSeinNetSubsystemTestAccess::InputDelay(*Net);
		// Catch-up heartbeats stay empty. Only the current boundary receives one
		// fitting prefix; the suffix is not assigned to a future turn early.
		FSeinNetSubsystemTestAccess::QueueThrough(*Net, FirstTurn + 2, true);
		ASSERT_THAT(AreEqual(3, FSeinNetSubsystemTestAccess::PendingTurnCount(*Net)));
		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::FrozenDraftCount(*Net, 0)));
		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::FrozenDraftCount(*Net, 1)));
		ASSERT_THAT(AreEqual(2, FSeinNetSubsystemTestAccess::FrozenDraftCount(*Net, 2)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::FrozenDraftTick(*Net, 2, 0)));
		ASSERT_THAT(AreEqual(2, FSeinNetSubsystemTestAccess::FrozenDraftTick(*Net, 2, 1)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::PendingCommandCount(*Net)));

		FSeinNetSubsystemTestAccess::QueueThrough(*Net, FirstTurn + 3, true);
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::FrozenDraftCount(*Net, 3)));
		ASSERT_THAT(AreEqual(3, FSeinNetSubsystemTestAccess::FrozenDraftTick(*Net, 3, 0)));
		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::PendingCommandCount(*Net)));
	}

	TEST(LocalBurstPrefixesHonorWireAndCanonicalCostShares,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
		USeinNetSubsystem* WireNet = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(WireNet));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(
			*WireNet, /*AuthorCount=*/16, /*MaxCommandsPerSubmission=*/16);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
			*WireNet, FSeinNetSubsystemTestAccess::WidePing(60000, 1))));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
			*WireNet, FSeinNetSubsystemTestAccess::WidePing(60000, 2))));
		const int32 WireTurn = FSeinNetSubsystemTestAccess::InputDelay(*WireNet);
		FSeinNetSubsystemTestAccess::QueueThrough(*WireNet, WireTurn, true);
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::FrozenDraftCount(*WireNet, 0)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::PendingCommandCount(*WireNet)));

		USeinNetSubsystem* AllocationNet = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(AllocationNet));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(
			*AllocationNet, /*AuthorCount=*/16, /*MaxCommandsPerSubmission=*/16);
		// Native padding may increase this receiver's local decode charge, but it
		// cannot change a consensus author share. The bounded native ceiling is
		// enforced independently by the decoder, not by replicated admission.
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
			*AllocationNet,
			FSeinNetSubsystemTestAccess::ExpandedPayloadPing(300, 99))));
		for (int32 Marker = 1; Marker <= 3; ++Marker)
		{
			ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
				*AllocationNet,
				FSeinNetSubsystemTestAccess::ExpandedPayloadPing(100, Marker))));
		}
		const int32 AllocationTurn = FSeinNetSubsystemTestAccess::InputDelay(*AllocationNet);
		FSeinNetSubsystemTestAccess::QueueThrough(
			*AllocationNet, AllocationTurn, true);
		ASSERT_THAT(AreEqual(4,
			FSeinNetSubsystemTestAccess::FrozenDraftCount(*AllocationNet, 0)));
		ASSERT_THAT(AreEqual(0,
			FSeinNetSubsystemTestAccess::PendingCommandCount(*AllocationNet)));
	}

	TEST(LocalAndAIBacklogsAreBoundedAndAIPeekIsTransactional,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
		TestRunner->AddExpectedError(
			TEXT("bounded outgoing backlog is full"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("bounded AI backlog is full"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		USeinNetSubsystem* LocalNet = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(LocalNet));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(
			*LocalNet, /*AuthorCount=*/1, /*MaxCommandsPerSubmission=*/1);
		for (int32 Marker = 1; Marker <= 4; ++Marker)
		{
			ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBuffer(
				*LocalNet, FSeinNetSubsystemTestAccess::Ping(Marker))));
		}
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::TryBuffer(
			*LocalNet, FSeinNetSubsystemTestAccess::Ping(5))));
		const int32 Turn = FSeinNetSubsystemTestAccess::InputDelay(*LocalNet);
		FSeinNetSubsystemTestAccess::QueueThrough(*LocalNet, Turn, true);
		ASSERT_THAT(AreEqual(1,
			FSeinNetSubsystemTestAccess::FrozenDraftCount(*LocalNet, 0)));
		ASSERT_THAT(AreEqual(3,
			FSeinNetSubsystemTestAccess::PendingCommandCount(*LocalNet)));

		USeinNetSubsystem* AINet = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(AINet));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(
			*AINet, /*AuthorCount=*/1, /*MaxCommandsPerSubmission=*/1);
		const FSeinPlayerID Slot(1);
		FSeinNetSubsystemTestAccess::SetLifecycle(
			*AINet, Slot, ESeinSlotLifecycle::AITakeover);
		for (int32 Marker = 1; Marker <= 4; ++Marker)
		{
			ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBufferAICommand(
				*AINet, Slot, FSeinNetSubsystemTestAccess::Ping(Marker))));
		}
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::TryBufferAICommand(
			*AINet, Slot, FSeinNetSubsystemTestAccess::Ping(5))));

		TArray<FSeinCommand> Prefix;
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::BuildDroppedSubmission(
			*AINet, Slot, Turn, true, Prefix)));
		ASSERT_THAT(AreEqual(1, Prefix.Num()));
		ASSERT_THAT(AreEqual(4,
			FSeinNetSubsystemTestAccess::PendingAICommandCount(*AINet, Slot)));
		FSeinNetSubsystemTestAccess::ConsumeAICommands(*AINet, Slot, Prefix.Num());
		ASSERT_THAT(AreEqual(3,
			FSeinNetSubsystemTestAccess::PendingAICommandCount(*AINet, Slot)));
	}

	TEST(AuthorSubmissionCommandCapIsMatchFrozen,
		"SeinARTS.Unit.Network.Protocol")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(
			*Net, /*AuthorCount=*/1, /*MaxCommandsPerSubmission=*/7);
		FSeinNetSubsystemTestAccess::ResetEpoch(*Net);
		ASSERT_THAT(AreEqual(7,
			FSeinNetSubsystemTestAccess::FrozenMaxCommands(*Net)));
		FSeinNetSubsystemTestAccess::ResetMatch(*Net);
		ASSERT_THAT(AreEqual(0,
			FSeinNetSubsystemTestAccess::FrozenMaxCommands(*Net)));
	}

	TEST(AIIngressRequiresManifestTakeoverAndCannotGrowArbitrarySlotQueues,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
		TestRunner->AddExpectedError(
			TEXT("only an AI-takeover slot may author AI commands"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("slot is absent from the frozen command-author manifest"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);
		const FSeinPlayerID OwnedSlot(1);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::TryBufferAICommand(
			*Net, OwnedSlot, FSeinNetSubsystemTestAccess::Ping(1))));
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::TryBufferAICommand(
			*Net, FSeinPlayerID(2), FSeinNetSubsystemTestAccess::Ping(2))));
		FSeinNetSubsystemTestAccess::SetLifecycle(
			*Net, OwnedSlot, ESeinSlotLifecycle::Dropped);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::TryBufferAICommand(
			*Net, OwnedSlot, FSeinNetSubsystemTestAccess::Ping(3))));
		ASSERT_THAT(AreEqual(0,
			FSeinNetSubsystemTestAccess::PendingAIBacklogCount(*Net)));

		FSeinNetSubsystemTestAccess::SetLifecycle(
			*Net, OwnedSlot, ESeinSlotLifecycle::AITakeover);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::TryBufferAICommand(
			*Net, OwnedSlot, FSeinNetSubsystemTestAccess::Ping(4))));
		ASSERT_THAT(AreEqual(1,
			FSeinNetSubsystemTestAccess::PendingAIBacklogCount(*Net)));
	}

	TEST(AIIngressRejectsEveryUnsupportedNetworkPauseControl,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
		TestRunner->AddExpectedError(
			TEXT("dropping unsupported network pause-control"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);
		const FSeinPlayerID Slot(1);
		FSeinNetSubsystemTestAccess::SetLifecycle(
			*Net, Slot, ESeinSlotLifecycle::AITakeover);

		FSeinCommand Pause;
		Pause.CommandType = SeinARTSTags::Command_Type_PauseMatchRequest;
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::TryBufferAICommand(
			*Net, Slot, Pause)));
		FSeinCommand Resume;
		Resume.CommandType = SeinARTSTags::Command_Type_ResumeMatchRequest;
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::TryBufferAICommand(
			*Net, Slot, Resume)));
		ASSERT_THAT(AreEqual(0,
			FSeinNetSubsystemTestAccess::PendingAIBacklogCount(*Net)));
	}

	TEST(PendingWorldRootRetriesExact128BitCheckpoint, "SeinARTS.Unit.Network")
	{
		TestRunner->AddExpectedError(
			TEXT("FlushPendingWorldStateRootReports: retaining exact turn=10 root="),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));

		const FGuid CapturedRoot(
			0x12345678u, 0x90ABCDEFu, 0x0BADF00Du, 0xCAFEBABEu);
		FSeinNetSubsystemTestAccess::EnqueueWorldRoot(
			*Net, 10, CapturedRoot);
		FSeinNetSubsystemTestAccess::SetWorldRootTransport(*Net,
			[](int32, const FGuid&) { return false; });
		FSeinNetSubsystemTestAccess::FlushWorldRoots(*Net);
		ASSERT_THAT(AreEqual(
			1,
			FSeinNetSubsystemTestAccess::PendingWorldRootCount(*Net)));
		ASSERT_THAT(AreEqual(
			-1,
			FSeinNetSubsystemTestAccess::LastWorldRootReported(*Net)));

		int32 SentTurn = INDEX_NONE;
		FGuid SentRoot;
		FSeinNetSubsystemTestAccess::SetWorldRootTransport(*Net,
			[&](int32 Turn, const FGuid& WorldRoot)
			{
				SentTurn = Turn;
				SentRoot = WorldRoot;
				return true;
			});
		FSeinNetSubsystemTestAccess::FlushWorldRoots(*Net);
		ASSERT_THAT(AreEqual(10, SentTurn));
		ASSERT_THAT(IsTrue(SentRoot == CapturedRoot));
		ASSERT_THAT(AreEqual(
			0,
			FSeinNetSubsystemTestAccess::PendingWorldRootCount(*Net)));
		ASSERT_THAT(AreEqual(
			10,
			FSeinNetSubsystemTestAccess::LastWorldRootReported(*Net)));
	}

	TEST(SoleReporterSkipsRoutineWorldRootCapture,
		"SeinARTS.Unit.Network.Protocol")
	{
		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net, 1);
		FSeinNetSubsystemTestAccess::SetNetworkingActive(*Net, true);
		FSeinNetSubsystemTestAccess::
			SetDeterminismGossipEnabledWithoutScheduleOverride(*Net);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 10);
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Net, FSeinNetSubsystemTestAccess::Participant());

		bool bRootCaptureAttempted = false;
		FSeinNetSubsystemTestAccess::SetWorldRootResolver(
			*Net,
			[&bRootCaptureAttempted](FGuid& OutRoot, FString&)
			{
				bRootCaptureAttempted = true;
				OutRoot = FGuid(1, 2, 3, 4);
				return true;
			});
		bool bTransportAttempted = false;
		FSeinNetSubsystemTestAccess::SetWorldRootTransport(
			*Net,
			[&bTransportAttempted](int32, const FGuid&)
			{
				bTransportAttempted = true;
				return true;
			});

		FSeinNetSubsystemTestAccess::MaybeSubmitWorldRoot(*Net, 10);

		ASSERT_THAT(IsFalse(bRootCaptureAttempted));
		ASSERT_THAT(IsFalse(bTransportAttempted));
		ASSERT_THAT(AreEqual(
			0,
			FSeinNetSubsystemTestAccess::PendingWorldRootCount(*Net)));
		ASSERT_THAT(AreEqual(
			-1,
			FSeinNetSubsystemTestAccess::LastWorldRootReported(*Net)));
	}

	TEST(ClientManifestPeerSubmitsWithoutCoordinatorRelayMap,
		"SeinARTS.Unit.Network.Protocol")
	{
		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		// Client-shaped state: the frozen manifest contains both simulation
		// participants, while the coordinator-only RelayToParticipant map is
		// intentionally empty.
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net, 2);
		FSeinNetSubsystemTestAccess::SetNetworkingActive(*Net, true);
		FSeinNetSubsystemTestAccess::
			SetDeterminismGossipEnabledWithoutScheduleOverride(*Net);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 30);
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Net, FSeinNetSubsystemTestAccess::Participant(2));

		bool bRootCaptureAttempted = false;
		const FGuid CapturedRoot(9, 8, 7, 6);
		FSeinNetSubsystemTestAccess::SetWorldRootResolver(
			*Net,
			[&](FGuid& OutRoot, FString&)
			{
				bRootCaptureAttempted = true;
				OutRoot = CapturedRoot;
				return true;
			});
		int32 SentTurn = INDEX_NONE;
		FGuid SentRoot;
		FSeinNetSubsystemTestAccess::SetWorldRootTransport(
			*Net,
			[&](int32 Turn, const FGuid& WorldRoot)
			{
				SentTurn = Turn;
				SentRoot = WorldRoot;
				return true;
			});

		FSeinNetSubsystemTestAccess::MaybeSubmitWorldRoot(*Net, 30);

		ASSERT_THAT(IsTrue(bRootCaptureAttempted));
		ASSERT_THAT(AreEqual(30, SentTurn));
		ASSERT_THAT(IsTrue(SentRoot == CapturedRoot));
		ASSERT_THAT(AreEqual(
			30,
			FSeinNetSubsystemTestAccess::LastWorldRootReported(*Net)));
	}

	TEST(AuthenticatedBootstrapManifestPinsClientReporterTopology,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		const TArray<FSeinParticipantBinding> Bindings{
			FSeinNetSubsystemTestAccess::Binding(1, 1),
			FSeinNetSubsystemTestAccess::Binding(2, 2)};
		const FSeinProtocolContext Context =
			FSeinNetSubsystemTestAccess::ContextFor(Bindings);
		FSeinMatchSettings MatchSettings;
		for (int32 SlotIndex = 1; SlotIndex <= 2; ++SlotIndex)
		{
			FSeinMatchSlot& Slot = MatchSettings.Slots.Emplace_GetRef();
			Slot.SlotIndex = SlotIndex;
			Slot.State = ESeinSlotState::Human;
		}

		TArray<FSeinParticipantBinding> CanonicalBindings;
		TMap<FSeinPlayerID, FSeinNetworkParticipantID> SlotMap;
		FString Error;
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				ValidateParticipantManifestAssignment(
					*Net,
					Context,
					FSeinPlayerID(2),
					FSeinNetSubsystemTestAccess::Participant(2),
					/*bLocalSimulates=*/true,
					Bindings,
					MatchSettings,
					CanonicalBindings,
					SlotMap,
					Error)));
		ASSERT_THAT(AreEqual(2, CanonicalBindings.Num()));
		ASSERT_THAT(IsTrue(
			SlotMap.FindRef(FSeinPlayerID(2))
				== FSeinNetSubsystemTestAccess::Participant(2)));

		TArray<FSeinParticipantBinding> TamperedBindings = Bindings;
		TamperedBindings[1].bReportsWorldRoots = false;
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::
				ValidateParticipantManifestAssignment(
					*Net,
					Context,
					FSeinPlayerID(2),
					FSeinNetSubsystemTestAccess::Participant(2),
					/*bLocalSimulates=*/true,
					TamperedBindings,
					MatchSettings,
					CanonicalBindings,
					SlotMap,
					Error)));
		ASSERT_THAT(AreEqual(0, CanonicalBindings.Num()));
		ASSERT_THAT(AreEqual(0, SlotMap.Num()));

		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::
				ValidateParticipantManifestAssignment(
					*Net,
					Context,
					FSeinPlayerID(2),
					FSeinNetSubsystemTestAccess::Participant(2),
					/*bLocalSimulates=*/false,
					Bindings,
					MatchSettings,
					CanonicalBindings,
					SlotMap,
					Error)));
	}

	TEST(InvalidWorldRootCannotEnterRetryQueue, "SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("EnqueueWorldStateRootReport: refusing invalid root"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));

		FSeinNetSubsystemTestAccess::EnqueueWorldRoot(*Net, 10, FGuid());
		ASSERT_THAT(AreEqual(
			0,
			FSeinNetSubsystemTestAccess::PendingWorldRootCount(*Net)));
		ASSERT_THAT(AreEqual(
			-1,
			FSeinNetSubsystemTestAccess::LastWorldRootReported(*Net)));
	}

	TEST(DueWorldRootCaptureFailureStopsGateAndRetriesExactCheckpoint,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("canonical root unavailable at due checkpoint turn 10"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("[DETERMINISM SESSION FAILED]"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("retaining session-failure report kind=1 turn=10"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net, 2);
		FSeinNetSubsystemTestAccess::SetNetworkingActive(*Net, true);
		FSeinNetSubsystemTestAccess::SetDeterminismSchedule(
			*Net, true, 10);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 10);
		const FSeinNetworkParticipantID LocalParticipant =
			FSeinNetSubsystemTestAccess::Participant(2);
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Net, LocalParticipant);
		FSeinNetSubsystemTestAccess::SetWorldRootResolver(
			*Net,
			[](FGuid&, FString& OutError)
			{
				OutError = TEXT("forced canonical composer failure");
				return false;
			});
		FSeinNetSubsystemTestAccess::SetDeterminismFailureTransport(
			*Net,
			[](const FSeinDeterminismSessionFailure&) { return false; });

		FSeinNetSubsystemTestAccess::MaybeSubmitWorldRoot(*Net, 10);

		const FSeinDeterminismSessionFailure& Failure =
			FSeinNetSubsystemTestAccess::DeterminismFailure(*Net);
		ASSERT_THAT(IsTrue(Failure.IsValid()));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(
				ESeinDeterminismSessionFailureKind::
					CanonicalRootCaptureFailed),
			static_cast<int32>(Failure.Kind)));
		ASSERT_THAT(AreEqual(10, Failure.Turn));
		ASSERT_THAT(IsTrue(Failure.ParticipantID == LocalParticipant));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::
				IsDeterminismFailureAuthoritative(*Net)));
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				HasPendingDeterminismFailure(*Net)));
		ASSERT_THAT(AreEqual(
			0,
			FSeinNetSubsystemTestAccess::PendingWorldRootCount(*Net)));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::IsTurnReady(*Net, 0)));

		int32 SentTurn = INDEX_NONE;
		FSeinNetSubsystemTestAccess::SetDeterminismFailureTransport(
			*Net,
			[&SentTurn](const FSeinDeterminismSessionFailure& Pending)
			{
				SentTurn = Pending.Turn;
				return true;
			});
		FSeinNetSubsystemTestAccess::FlushDeterminismFailures(*Net);
		ASSERT_THAT(AreEqual(10, SentTurn));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::
				HasPendingDeterminismFailure(*Net)));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::IsTurnReady(*Net, 0)));
	}

	TEST(CoordinatorPruneFailsZeroReportDueCheckpointClosed,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("[DETERMINISM SESSION FAILED]"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);
		FSeinNetSubsystemTestAccess::SetServer(*Net, true);
		FSeinNetSubsystemTestAccess::SetDedicatedAuthority(*Net, true);
		FSeinNetSubsystemTestAccess::SetDeterminismSchedule(
			*Net, true, 10);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 266);
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Net, FSeinNetSubsystemTestAccess::Participant());

		// No per-turn report map exists at all. Crossing the retention boundary
		// must still identify the first due checkpoint as a failed obligation.
		FSeinNetSubsystemTestAccess::Prune(*Net, 266);

		const FSeinDeterminismSessionFailure& Failure =
			FSeinNetSubsystemTestAccess::DeterminismFailure(*Net);
		ASSERT_THAT(AreEqual(
			static_cast<int32>(
				ESeinDeterminismSessionFailureKind::
					CanonicalRootCheckpointExpired),
			static_cast<int32>(Failure.Kind)));
		ASSERT_THAT(AreEqual(10, Failure.Turn));
		ASSERT_THAT(IsTrue(
			Failure.ParticipantID
				== FSeinNetSubsystemTestAccess::Participant()));
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				IsDeterminismFailureAuthoritative(*Net)));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::IsTurnReady(*Net, 0)));
		ASSERT_THAT(AreEqual(
			10,
			FSeinNetSubsystemTestAccess::WorldRootFloor(*Net)));
	}

	TEST(CustomPeerAuthorityUsesTopologyNeutralRootFailureIngress,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("[DETERMINISM SESSION FAILED]"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);
		FSeinNetSubsystemTestAccess::SetDeterminismSchedule(
			*Net, true, 10);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 9);
		const FSeinNetworkParticipantID Coordinator =
			FSeinNetSubsystemTestAccess::Participant();
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Net, Coordinator);

		// No UE server/dedicated override is set: a trusted custom/P2P adapter
		// can still enter the same coordinator-owned protocol state.
		ASSERT_THAT(IsTrue(Net->IsLocalProtocolCoordinator()));
		FSeinDeterminismSessionFailure FutureCaptureFailure;
		FutureCaptureFailure.Kind =
			ESeinDeterminismSessionFailureKind::
				CanonicalRootCaptureFailed;
		FutureCaptureFailure.Turn = 10;
		FutureCaptureFailure.ParticipantID = Coordinator;
		ASSERT_THAT(IsTrue(
			Net->HandleAuthenticatedDeterminismSessionFailure(
				FSeinNetSubsystemTestAccess::Context(*Net),
				Coordinator,
				FutureCaptureFailure)));
		ASSERT_THAT(IsFalse(Net->HasDeterminismSessionFailure()));

		// A faster reporter may legitimately reach the checkpoint first.
		// The coordinator accepts the authenticated proof, then applies it
		// canonically when its own simulation reaches that same turn.
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 10);
		FSeinNetSubsystemTestAccess::Prune(*Net, 10);
		ASSERT_THAT(IsTrue(Net->HasDeterminismSessionFailure()));
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				IsDeterminismFailureAuthoritative(*Net)));
	}

	TEST(LocalTopologyInvalidationFailsClosedAndRetriesExactReport,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("local execution topology invalidated at turn=7"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("[DETERMINISM SESSION FAILED]"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("retaining session-failure report kind=3 turn=7"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net, 2);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 7);
		const FSeinNetworkParticipantID LocalParticipant =
			FSeinNetSubsystemTestAccess::Participant(2);
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Net, LocalParticipant);
		bool bCustomTransportReady = false;
		FSeinDeterminismSessionFailure SentFailure;
		FSeinNetSubsystemTestAccess::
			SetCustomDeterminismFailureSubmitter(
			*Net,
			[&](
				const FSeinProtocolContext& Context,
				const FSeinDeterminismSessionFailure& Pending)
			{
				if (!bCustomTransportReady) return false;
				if (Context
					!= FSeinNetSubsystemTestAccess::Context(*Net))
				{
					return false;
				}
				SentFailure = Pending;
				return true;
			});

		FSeinNetSubsystemTestAccess::ReportLocalTopologyFailure(*Net);

		const FSeinDeterminismSessionFailure& Failure =
			FSeinNetSubsystemTestAccess::DeterminismFailure(*Net);
		ASSERT_THAT(IsTrue(Failure.IsValid()));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(
				ESeinDeterminismSessionFailureKind::
					ExecutionTopologyInvalidated),
			static_cast<int32>(Failure.Kind)));
		ASSERT_THAT(AreEqual(7, Failure.Turn));
		ASSERT_THAT(IsTrue(
			Failure.ParticipantID == LocalParticipant));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::
				IsDeterminismFailureAuthoritative(*Net)));
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				HasPendingDeterminismFailure(*Net)));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::IsTurnReady(*Net, 0)));

		bCustomTransportReady = true;
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				RetryDeterminismFailure(*Net)));
		ASSERT_THAT(IsTrue(SentFailure == Failure));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::
				HasPendingDeterminismFailure(*Net)));
	}

	TEST(FutureTopologyFailureUsesAuthenticatedSourceAndCanonicalTurn,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("[DETERMINISM SESSION FAILED]"),
			EAutomationExpectedErrorFlags::Contains, 1, false);

		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net, 2);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Net, 5);
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Net, FSeinNetSubsystemTestAccess::Participant());

		const FSeinNetworkParticipantID AuthenticatedPeer =
			FSeinNetSubsystemTestAccess::Participant(2);
		FSeinDeterminismSessionFailure SpoofedFailure;
		SpoofedFailure.Kind =
			ESeinDeterminismSessionFailureKind::
				ExecutionTopologyInvalidated;
		SpoofedFailure.Turn = 7;
		SpoofedFailure.ParticipantID =
			FSeinNetSubsystemTestAccess::Participant(999);

		ASSERT_THAT(IsTrue(
			Net->HandleAuthenticatedDeterminismSessionFailure(
				FSeinNetSubsystemTestAccess::Context(*Net),
				AuthenticatedPeer,
				SpoofedFailure)));
		ASSERT_THAT(IsFalse(Net->HasDeterminismSessionFailure()));

		FSeinNetSubsystemTestAccess::ApplyDueDeterminismFailures(
			*Net, 6);
		ASSERT_THAT(IsFalse(Net->HasDeterminismSessionFailure()));
		FSeinNetSubsystemTestAccess::ApplyDueDeterminismFailures(
			*Net, 7);

		const FSeinDeterminismSessionFailure& Failure =
			FSeinNetSubsystemTestAccess::DeterminismFailure(*Net);
		ASSERT_THAT(IsTrue(Failure.IsValid()));
		ASSERT_THAT(AreEqual(7, Failure.Turn));
		ASSERT_THAT(IsTrue(
			Failure.ParticipantID == AuthenticatedPeer));
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				IsDeterminismFailureAuthoritative(*Net)));
	}

	TEST(DedicatedAndRemotePeersAcceptTopologyFailureWithoutRootCheckpoint,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("[DETERMINISM SESSION FAILED]"),
			EAutomationExpectedErrorFlags::Contains, 2, false);

		USeinNetSubsystem* Dedicated =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Dedicated));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Dedicated);
		FSeinNetSubsystemTestAccess::SetServer(*Dedicated, true);
		FSeinNetSubsystemTestAccess::SetDedicatedAuthority(
			*Dedicated, true);
		FSeinNetSubsystemTestAccess::SetLocalParticipant(
			*Dedicated,
			FSeinNetSubsystemTestAccess::Participant());
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*Dedicated, 0);

		FSeinDeterminismSessionFailure TurnZeroFailure;
		TurnZeroFailure.Kind =
			ESeinDeterminismSessionFailureKind::
				ExecutionTopologyInvalidated;
		TurnZeroFailure.Turn = 0;
		TurnZeroFailure.ParticipantID =
			FSeinNetSubsystemTestAccess::Participant(77);
		ASSERT_THAT(IsTrue(
			Dedicated->HandleAuthenticatedDeterminismSessionFailure(
				FSeinNetSubsystemTestAccess::Context(*Dedicated),
				FSeinNetSubsystemTestAccess::Participant(),
				TurnZeroFailure)));
		ASSERT_THAT(IsTrue(Dedicated->HasDeterminismSessionFailure()));

		USeinNetSubsystem* RemotePeer =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(RemotePeer));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*RemotePeer, 2);
		FSeinNetSubsystemTestAccess::SetCurrentTurn(*RemotePeer, 1);
		FSeinDeterminismSessionFailure AuthoritativeFailure;
		AuthoritativeFailure.Kind =
			ESeinDeterminismSessionFailureKind::
				ExecutionTopologyInvalidated;
		AuthoritativeFailure.Turn = 2;
		AuthoritativeFailure.ParticipantID =
			FSeinNetSubsystemTestAccess::Participant(2);
		RemotePeer->HandleAuthoritativeDeterminismSessionFailure(
			FSeinNetSubsystemTestAccess::Context(*RemotePeer),
			AuthoritativeFailure);
		ASSERT_THAT(IsTrue(RemotePeer->HasDeterminismSessionFailure()));
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::
				IsDeterminismFailureAuthoritative(*RemotePeer)));
	}

	TEST(NetworkPauseControlFailsClosedOutsideCanonicalLane,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("rejected unsupported network pause-control"),
			EAutomationExpectedErrorFlags::Contains, 2, false);
		TestRunner->AddExpectedError(
			TEXT("ordinary ingress requires a launched, running match"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);
		FSeinNetSubsystemTestAccess::SetNetworkingActive(*Net, true);

		FSeinCommand Resume;
		Resume.CommandType = SeinARTSTags::Command_Type_ResumeMatchRequest;
		Net->SubmitLocalCommandDraft(
			Resume, /*bRequestMatchAdministration=*/true);

		FSeinCommand Pause;
		Pause.CommandType = SeinARTSTags::Command_Type_PauseMatchRequest;
		Net->SubmitLocalCommand(Pause);
		ASSERT_THAT(AreEqual(
			0, FSeinNetSubsystemTestAccess::PendingCommandCount(*Net)));

		// Pause control is rejected before ordinary ingress, while an ordinary
		// command still needs the shared Consumed + running lifecycle gate.
		FSeinCommand Ping;
		Ping.CommandType = SeinARTSTags::Command_Type_Ping;
		Ping.DerivedResourcePayer = FSeinPlayerID(1);
		Net->SubmitLocalCommand(Ping);
		ASSERT_THAT(AreEqual(
			0, FSeinNetSubsystemTestAccess::PendingCommandCount(*Net)));
	}

	TEST(EpochResetIsCompleteButDoesNotChooseMatchSemantics, "SeinARTS.Unit.Network")
	{
		TestRunner->AddExpectedError(
			TEXT("Test: rejecting negative TurnId=-1."),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("Test: rejecting implausible future TurnId=100000"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedEpochState(*Net);
		FSeinNetSubsystemTestAccess::ResetEpoch(*Net);

		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::IsEpochStateClear(*Net)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::MatchStateWasPreserved(*Net)));
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::AcceptsCommandTurn(*Net, -1)));
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::AcceptsCommandTurn(*Net, 100000)));
	}

	TEST(EpochResetRemovesBothWorldScopedNetHooks,
		"SeinARTS.Unit.Network.Protocol")
	{
		USeinNetSubsystem* Net =
			FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		int32 TickCallbackCount = 0;
		int32 TopologyCallbackCount = 0;
		USeinWorldSubsystem* WorldSub =
			FSeinNetSubsystemTestAccess::InstallSyntheticWorldHooks(
				*Net,
				TickCallbackCount,
				TopologyCallbackCount);
		ASSERT_THAT(IsNotNull(WorldSub));

		WorldSub->OnSimTickCompleted.Broadcast(1);
		WorldSub->OnExecutionTopologyInvalidated.Broadcast(
			TEXT("before reset"));
		ASSERT_THAT(AreEqual(1, TickCallbackCount));
		ASSERT_THAT(AreEqual(1, TopologyCallbackCount));

		FSeinNetSubsystemTestAccess::ResetEpoch(*Net);
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::AreWorldHookHandlesClear(*Net)));
		WorldSub->OnSimTickCompleted.Broadcast(2);
		WorldSub->OnExecutionTopologyInvalidated.Broadcast(
			TEXT("after reset"));
		ASSERT_THAT(AreEqual(1, TickCallbackCount));
		ASSERT_THAT(AreEqual(1, TopologyCallbackCount));
	}

	TEST(CompletedProtocolHistoryIsBounded, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedAndPruneCompletedHistory(*Net, 600);

		ASSERT_THAT(AreEqual(256, FSeinNetSubsystemTestAccess::CompletedTurnCount(*Net)));
		ASSERT_THAT(AreEqual(
			256,
			FSeinNetSubsystemTestAccess::CompletedWorldRootCount(*Net)));
		ASSERT_THAT(AreEqual(344, FSeinNetSubsystemTestAccess::TurnFloor(*Net)));
		ASSERT_THAT(AreEqual(
			344,
			FSeinNetSubsystemTestAccess::WorldRootFloor(*Net)));
	}

	TEST(DedicatedAuthorityNeedsNoLocalRelayAndReceivesCanonicalTurn, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SetDedicatedAuthority(*Net, true);

		// A dedicated authority still requires an established seed and bound
		// world hooks, but never a synthetic local relay or player slot.
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::StartPrerequisitesReady(*Net, true)));
		FSeinNetSubsystemTestAccess::SetSessionSeed(*Net, 987654321);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::StartPrerequisitesReady(*Net, false)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::StartPrerequisitesReady(*Net, true)));

		FSeinCommand Command;
		Command.CommandType = SeinARTSTags::Command_Type_Ping;
		Command.SchemaVersion = 1;
		Command.Tick = 444;
		TArray<FSeinCommand> CanonicalTurn;
		CanonicalTurn.Add(Command);
		FSeinNetSubsystemTestAccess::BufferDedicatedAuthorityTurn(*Net, 7, CanonicalTurn);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::HasReceivedTurn(*Net, 7)));
		ASSERT_THAT(AreEqual(444, FSeinNetSubsystemTestAccess::ReceivedCommandTick(*Net, 7)));

		// The same direct path must not duplicate listen/client relay delivery.
		FSeinNetSubsystemTestAccess::SetDedicatedAuthority(*Net, false);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::StartPrerequisitesReady(*Net, true)));
		FSeinNetSubsystemTestAccess::BufferDedicatedAuthorityTurn(*Net, 8, CanonicalTurn);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::HasReceivedTurn(*Net, 8)));
	}

	TEST(WorldRootDuplicatesCompareAll128BitsAndFirstAcceptedWins,
		"SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinNetworkParticipantID Participant =
			FSeinNetSubsystemTestAccess::Participant();
		const FGuid FirstRoot(
			0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u);
		const FGuid ConflictingHighBits(
			0x11111111u, 0x22222222u, 0x33333333u, 0x55555555u);

		ASSERT_THAT(AreEqual(
			0,
			FSeinNetSubsystemTestAccess::BufferWorldRootFirstWins(
				*Net, 10, Participant, FirstRoot)));
		ASSERT_THAT(AreEqual(
			1,
			FSeinNetSubsystemTestAccess::BufferWorldRootFirstWins(
				*Net, 10, Participant, FirstRoot)));
		ASSERT_THAT(AreEqual(
			2,
			FSeinNetSubsystemTestAccess::BufferWorldRootFirstWins(
				*Net, 10, Participant, ConflictingHighBits)));
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::StoredWorldRoot(
				*Net, 10, Participant)
			== FirstRoot));
	}

	TEST(OnlyConnectedLifecycleMayAuthorRelayCommands, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinPlayerID Slot(1);

		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::CommandLifecycleAllowed(*Net, Slot)));
		FSeinNetSubsystemTestAccess::SetLifecycle(*Net, Slot, ESeinSlotLifecycle::Connected);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::CommandLifecycleAllowed(*Net, Slot)));
		FSeinNetSubsystemTestAccess::SetLifecycle(*Net, Slot, ESeinSlotLifecycle::Dropped);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::CommandLifecycleAllowed(*Net, Slot)));
		FSeinNetSubsystemTestAccess::SetLifecycle(*Net, Slot, ESeinSlotLifecycle::AITakeover);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::CommandLifecycleAllowed(*Net, Slot)));
		FSeinNetSubsystemTestAccess::SetLifecycle(*Net, Slot, ESeinSlotLifecycle::Reconnecting);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::CommandLifecycleAllowed(*Net, Slot)));
	}

	TEST(ConfigParityStartBarrierRequiresEveryExactReport, "SeinARTS.Unit.Network")
	{
		const FSeinNetworkParticipantID ParticipantA =
			FSeinNetSubsystemTestAccess::Participant(1);
		const FSeinNetworkParticipantID ParticipantB =
			FSeinNetSubsystemTestAccess::Participant(2);
		const int32 RequiredFingerprint = 0x13572468;
		const TArray<FSeinNetworkParticipantID> Expected{ParticipantA, ParticipantB};
		TMap<FSeinNetworkParticipantID, int32> Accepted;

		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
		Accepted.Add(ParticipantA, RequiredFingerprint);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
		// Logout/simulated disconnect removes SlotB from the connected expected
		// set, so the same deferred request becomes eligible immediately.
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			{ParticipantA}, Accepted, RequiredFingerprint)));

		// A received but rejected/stale report cannot satisfy the second slot.
		Accepted.Add(ParticipantB, 0x24681357);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
		Accepted.Add(ParticipantB, RequiredFingerprint);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
	}

	TEST(AITakeoverDrainAuthorityStampsSlotAndTurnTick, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);
		const FSeinPlayerID OwnedSlot(1);
		const int32 Turn = 9;
		FSeinNetSubsystemTestAccess::SetLifecycle(
			*Net, OwnedSlot, ESeinSlotLifecycle::AITakeover);

		FSeinCommand Forged;
		Forged.CommandType = SeinARTSTags::Command_Type_Ping;
		Forged.SchemaVersion = 1;
		Forged.PlayerID = FSeinPlayerID(7);
		Forged.Tick = -123;
		FSeinNetSubsystemTestAccess::BufferAICommand(*Net, OwnedSlot, Forged);

		TArray<FSeinCommand> Drained;
		// Recovery may complete older open turns, but must be heartbeat-only:
		// draining here would assign the slot-global AI queue to map order.
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::BuildDroppedSubmission(
			*Net, OwnedSlot, Turn - 1, false, Drained)));
		ASSERT_THAT(AreEqual(0, Drained.Num()));
		ASSERT_THAT(AreEqual(1,
			FSeinNetSubsystemTestAccess::PendingAICommandCount(*Net, OwnedSlot)));

		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::BuildDroppedSubmission(
			*Net, OwnedSlot, Turn, true, Drained)));
		ASSERT_THAT(AreEqual(1, Drained.Num()));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(OwnedSlot.Value), static_cast<int32>(Drained[0].PlayerID.Value)));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinCommandIssuerKind::Player),
			static_cast<int32>(Drained[0].IssuerKind)));
		ASSERT_THAT(AreEqual(
			Turn * FSeinNetSubsystemTestAccess::TicksPerTurn(*Net), Drained[0].Tick));
		FSeinNetSubsystemTestAccess::ConsumeAICommands(
			*Net, OwnedSlot, Drained.Num());
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::BuildDroppedSubmission(
			*Net, OwnedSlot, Turn + 1, true, Drained)));
	}

	TEST(AuthoritativeStampDiscardsWireIssuerForgery, "SeinARTS.Unit.Network.Protocol")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));

		FSeinCommand Forged;
		Forged.PlayerID = FSeinPlayerID(9);
		Forged.IssuerKind = ESeinCommandIssuerKind::DeterministicSystem;
		Forged.DerivedResourcePayer = FSeinPlayerID(9);
		Forged.Tick = -1;
		TArray<FSeinCommand> Batch{Forged};
		FSeinNetSubsystemTestAccess::StampBatch(
			*Net,
			Batch,
			FSeinPlayerID(2),
			/*bParticipantCanAdministerMatch=*/false,
			7);
		ASSERT_THAT(AreEqual(2, static_cast<int32>(Batch[0].PlayerID.Value)));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinCommandIssuerKind::Player),
			static_cast<int32>(Batch[0].IssuerKind)));
		ASSERT_THAT(IsTrue(Batch[0].DerivedResourcePayer.IsNeutral()));
		ASSERT_THAT(AreEqual(
			7 * FSeinNetSubsystemTestAccess::TicksPerTurn(*Net), Batch[0].Tick));

		// Capability alone never elevates unknown or ordinary gameplay schemas.
		FSeinNetSubsystemTestAccess::StampBatch(
			*Net,
			Batch,
			FSeinPlayerID(2),
			/*bParticipantCanAdministerMatch=*/true,
			8);
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinCommandIssuerKind::Player),
			static_cast<int32>(Batch[0].IssuerKind)));

		Batch[0].CommandType = SeinARTSTags::Command_Type_Ping;
		FSeinNetSubsystemTestAccess::StampBatch(
			*Net, Batch, FSeinPlayerID(2),
			/*bParticipantCanAdministerMatch=*/true, 9);
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinCommandIssuerKind::Player),
			static_cast<int32>(Batch[0].IssuerKind)));

		// Only a registered MatchControl schema receives the authenticated
		// participant's administrative capability.
		Batch[0].CommandType = SeinARTSTags::Command_Type_EndMatch;
		FSeinNetSubsystemTestAccess::StampBatch(
			*Net, Batch, FSeinPlayerID(2),
			/*bParticipantCanAdministerMatch=*/true, 10);
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinCommandIssuerKind::MatchAdministrator),
			static_cast<int32>(Batch[0].IssuerKind)));
		ASSERT_THAT(IsTrue(Batch[0].PlayerID.IsNeutral()));
	}

	TEST(GateLateTurnsPreserveActualCompletingSubmitters, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinPlayerID SlotA(1);
		const FSeinPlayerID SlotB(2);
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net, 2);

		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::ResolveTurnReady(*Net, 7)));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::ResolveTurnReady(*Net, 8)));
		// SlotA is deliberately lower than canonical last SlotB: attribution
		// must follow the RPC that actually completed the turn, not sort order.
		FSeinNetSubsystemTestAccess::FinalizeTurnDiagnostics(*Net, 7, SlotA);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::HasIncompleteTurn(*Net, 7)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::HasIncompleteTurn(*Net, 8)));
		ASSERT_THAT(AreEqual(1, Net->GetTurnsCompletedCount()));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotA)));
		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotB)));

		// Completing the other interleaved turn must retain its own submitter.
		FSeinNetSubsystemTestAccess::FinalizeTurnDiagnostics(*Net, 8, SlotB);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::HasIncompleteTurn(*Net, 8)));
		ASSERT_THAT(AreEqual(2, Net->GetTurnsCompletedCount()));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotA)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotB)));

		// Ordinary interleaving can make one author complete an incomplete
		// aggregate, but it is not late unless the execution gate was reached.
		FSeinNetSubsystemTestAccess::MarkTurnIncomplete(*Net, 9);
		FSeinNetSubsystemTestAccess::FinalizeTurnDiagnostics(*Net, 9, SlotA);
		ASSERT_THAT(AreEqual(3, Net->GetTurnsCompletedCount()));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotA)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotB)));
	}

	TEST(NewAndContinueTravelHaveExplicitDurableSemantics, "SeinARTS.Unit.Network.Protocol")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::ConfigureTravelTest(*Net);
		const FSeinProtocolContext SourceContext =
			FSeinNetSubsystemTestAccess::Context(*Net);

		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::PrepareTravel(
			*Net, ESeinMatchTravelIntent::NewMatch)));
		const FSeinProtocolContext NewMatch =
			FSeinNetSubsystemTestAccess::PreparedContext(*Net);
		const int64 Seed = FSeinNetSubsystemTestAccess::PreparedSeed(*Net);
		ASSERT_THAT(IsTrue(NewMatch.IsValid()));
		ASSERT_THAT(AreEqual(static_cast<int64>(1), NewMatch.LockstepEpoch));
		ASSERT_THAT(IsTrue(
			NewMatch.SimulationContentDigest == FGuid(17, 18, 19, 20)));
		ASSERT_THAT(IsTrue(Seed != 0));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::IsDestinationPending(*Net)));
		ASSERT_THAT(IsTrue(SourceContext
			== FSeinNetSubsystemTestAccess::Context(*Net)));

		// A retry while the same destination is pending is an exact no-op.
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::PrepareTravel(
			*Net, ESeinMatchTravelIntent::NewMatch)));
		ASSERT_THAT(IsTrue(NewMatch
			== FSeinNetSubsystemTestAccess::PreparedContext(*Net)));
		ASSERT_THAT(AreEqual(
			Seed, FSeinNetSubsystemTestAccess::PreparedSeed(*Net)));

		FSeinNetSubsystemTestAccess::InstallReplayObjects(*Net);
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::HasReplayObjects(*Net)));
		FSeinNetSubsystemTestAccess::ConsumePreparedDestination(*Net);
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::HasReplayObjects(*Net)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::PrepareTravel(
			*Net, ESeinMatchTravelIntent::ContinueMatch)));
		const FSeinProtocolContext Continued =
			FSeinNetSubsystemTestAccess::PreparedContext(*Net);
		ASSERT_THAT(IsTrue(NewMatch
			== FSeinNetSubsystemTestAccess::Context(*Net)));
		ASSERT_THAT(IsTrue(Continued.MatchInstanceID == NewMatch.MatchInstanceID));
		ASSERT_THAT(AreEqual(
			Seed, FSeinNetSubsystemTestAccess::PreparedSeed(*Net)));
		ASSERT_THAT(AreEqual(static_cast<int64>(2), Continued.LockstepEpoch));
		ASSERT_THAT(AreEqual(NewMatch.CoordinatorTerm, Continued.CoordinatorTerm));
		ASSERT_THAT(IsTrue(NewMatch.MembershipDigest == Continued.MembershipDigest));
		ASSERT_THAT(IsTrue(
			NewMatch.SimulationContentDigest
				== Continued.SimulationContentDigest));

		FSeinNetSubsystemTestAccess::InstallReplayObjects(*Net);
		ASSERT_THAT(IsTrue(
			FSeinNetSubsystemTestAccess::HasReplayObjects(*Net)));
		FSeinNetSubsystemTestAccess::ConsumePreparedDestination(*Net);
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::HasReplayObjects(*Net)));
		ASSERT_THAT(IsTrue(Continued
			== FSeinNetSubsystemTestAccess::Context(*Net)));
	}

	TEST(AbortedPreparedTravelPreservesSourceProtocolAndReplay,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("Prepared match travel aborted without mutating the source epoch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::ConfigureTravelTest(*Net);
		const FSeinProtocolContext SourceContext =
			FSeinNetSubsystemTestAccess::Context(*Net);
		FSeinNetSubsystemTestAccess::InstallReplayObjects(*Net);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::PrepareTravel(
			*Net, ESeinMatchTravelIntent::NewMatch)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::HasReplayObjects(*Net)));

		Net->AbortPreparedMatchTravel(TEXT("test rejection"));
		ASSERT_THAT(IsFalse(
			FSeinNetSubsystemTestAccess::IsDestinationPending(*Net)));
		ASSERT_THAT(IsTrue(SourceContext
			== FSeinNetSubsystemTestAccess::Context(*Net)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::HasReplayObjects(*Net)));
	}

	TEST(PreparedWorldActivationDistinguishesSameMapTravelFromInPlaceStart,
		"SeinARTS.Unit.Network.Protocol")
	{
		UWorld* SourceWorld = NewObject<UWorld>();
		UWorld* DestinationWorld = NewObject<UWorld>();
		const FGuid Digest(1, 2, 3, 4);
		const FGuid OtherDigest(5, 6, 7, 8);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ActivationEligible(
			SourceWorld,
			SourceWorld,
			ESeinPreparedWorldActivation::RequiresWorldTransition,
			Digest,
			Digest)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::ActivationEligible(
			DestinationWorld,
			SourceWorld,
			ESeinPreparedWorldActivation::RequiresWorldTransition,
			Digest,
			Digest)));
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::ActivationEligible(
			SourceWorld,
			SourceWorld,
			ESeinPreparedWorldActivation::AllowCurrentWorld,
			Digest,
			Digest)));
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ActivationEligible(
			DestinationWorld,
			SourceWorld,
			ESeinPreparedWorldActivation::AllowCurrentWorld,
			OtherDigest,
			Digest)));
	}

	TEST(MismatchedProtocolContextCannotMutateClientTurnState, "SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("ClientHandleTurn: protocol context mismatch"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);

		FSeinProtocolContext Wrong = FSeinNetSubsystemTestAccess::Context(*Net);
		++Wrong.LockstepEpoch;
		FSeinCommand Command;
		Command.CommandType = SeinARTSTags::Command_Type_Ping;
		Command.SchemaVersion = 1;
		Command.PlayerID = FSeinPlayerID(1);
		Command.IssuerKind = ESeinCommandIssuerKind::Player;
		Command.Tick = 3 * FSeinNetSubsystemTestAccess::TicksPerTurn(*Net);
		FSeinNetSubsystemTestAccess::DeliverTurn(*Net, Wrong, 3, {Command});
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::HasReceivedTurn(*Net, 3)));

		FSeinNetSubsystemTestAccess::DeliverTurn(
			*Net, FSeinNetSubsystemTestAccess::Context(*Net), 3, {Command});
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::HasReceivedTurn(*Net, 3)));
	}

	TEST(CanonicalNetworkTurnRejectsExternalDerivedPayer,
		"SeinARTS.Unit.Network.Protocol")
	{
		TestRunner->AddExpectedError(
			TEXT("rejecting non-canonical assembled turn=3"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedConfiguredProtocol(*Net);

		FSeinCommand Forged;
		Forged.CommandType = SeinARTSTags::Command_Type_Ping;
		Forged.SchemaVersion = 1;
		Forged.PlayerID = FSeinPlayerID(1);
		Forged.IssuerKind = ESeinCommandIssuerKind::Player;
		Forged.DerivedResourcePayer = FSeinPlayerID(1);
		Forged.Tick = 3 * FSeinNetSubsystemTestAccess::TicksPerTurn(*Net);
		FSeinNetSubsystemTestAccess::DeliverTurn(
			*Net, FSeinNetSubsystemTestAccess::Context(*Net), 3, {Forged});
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::HasReceivedTurn(*Net, 3)));

		Forged.DerivedResourcePayer = FSeinPlayerID::Neutral();
		FSeinNetSubsystemTestAccess::DeliverTurn(
			*Net, FSeinNetSubsystemTestAccess::Context(*Net), 3, {Forged});
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::HasReceivedTurn(*Net, 3)));
	}

	TEST(OpaqueCommandRpcRejectsClaimedBytesBeforeAllocation,
		"SeinARTS.Unit.Network.Protocol.Security")
	{
		TArray<uint8> EncodedCount;
		FMemoryWriter Writer(EncodedCount, true);
		uint32 HostileCount = FSeinOpaqueCommandBatch::MaxBytes + 1u;
		Writer.SerializeIntPacked(HostileCount);

		FMemoryReader Reader(EncodedCount, true);
		FSeinOpaqueCommandBatch Batch;
		bool bSuccess = true;
		ASSERT_THAT(IsFalse(Batch.NetSerialize(Reader, nullptr, bSuccess)));
		ASSERT_THAT(IsFalse(bSuccess));
		ASSERT_THAT(AreEqual(0, Batch.Bytes.Num()));
	}
}
