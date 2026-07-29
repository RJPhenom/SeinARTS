#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Simulation/SeinTestSimContext.h"
#include "Data/SeinWorldSnapshot.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace
{
	struct FRunningMatchFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		FSeinPlayerID Player{1};

		FRunningMatchFixture()
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				return;
			}
			const auto AuthorState = [this]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));
			};
			if (!SeinTestMatchBootstrap::Materialize(*World, AuthorState))
			{
				World = nullptr;
				return;
			}
			if (!SeinTestMatchBootstrap::Start(*World))
			{
				World = nullptr;
				return;
			}
			FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		}

		~FRunningMatchFixture()
		{
			if (World)
			{
				World->StopSimulation();
			}
		}

		void TickPausedFrame(float DeltaMultiplier = 1.0f) const
		{
			FTSTicker::GetCoreTicker().Tick(
				World->GetFixedDeltaTimeSeconds() * DeltaMultiplier);
		}

		void SetPausedInSim(bool bPaused, bool bHard = false) const
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->SetSimPaused(bPaused, bHard);
		}

		FSeinCommand MakeCommand(FGameplayTag Type, int32 Tick = 0) const
		{
			FSeinCommand Command;
			Command.PlayerID = Player;
			Command.IssuerKind = ESeinCommandIssuerKind::Player;
			Command.CommandType = Type;
			Command.Tick = Tick;
			return Command;
		}

		FSeinCommand MakeResume(int32 Tick = 0) const
		{
			return MakeCommand(SeinARTSTags::Command_Type_ResumeMatchRequest, Tick);
		}
	};
}

namespace UE::SeinARTSTests
{
	TEST(PauseControlFreezesOrdinaryTimeAndResumesWithoutCatchUp,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		ASSERT_THAT(AreEqual(ESeinMatchState::Playing, Fixture.World->GetMatchState()));

		const int32 FrozenTick = Fixture.World->GetCurrentTick();
		int32 CompletedTicks = 0;
		Fixture.World->OnSimTickCompleted.AddLambda(
			[&CompletedTicks](int32) { ++CompletedTicks; });
		Fixture.SetPausedInSim(true);
		Fixture.TickPausedFrame(20.0f);

		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(FrozenTick, Fixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(0, CompletedTicks));
		ASSERT_THAT(IsTrue(Fixture.World->GetInterpolationAlpha() == 0.0f));

		bool bApplied = false;
		bool bStillPaused = true;
		bool bProtocolFailure = true;
		FGuid AppliedDigest;
		FGuid CallbackDigest;
		FString CallbackRootError;
		bool bCallbackRootAvailable = false;
		Fixture.World->PauseControlAppliedNotifier.BindLambda(
			[&](const FSeinPauseControlCursor&, bool bInStillPaused,
				const FGuid& Digest, bool bInProtocolFailure)
			{
				bApplied = true;
				bStillPaused = bInStillPaused;
				AppliedDigest = Digest;
				bProtocolFailure = bInProtocolFailure;
				bCallbackRootAvailable =
					Fixture.World->ComputeCanonicalStateRoot(
						CallbackDigest, CallbackRootError);
			});
		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		Fixture.TickPausedFrame(20.0f);

		ASSERT_THAT(IsTrue(bApplied));
		ASSERT_THAT(IsFalse(bStillPaused));
		ASSERT_THAT(IsFalse(bProtocolFailure));
		ASSERT_THAT(IsTrue(AppliedDigest.IsValid()));
		ASSERT_THAT(IsTrue(bCallbackRootAvailable));
		ASSERT_THAT(IsTrue(CallbackRootError.IsEmpty()));
		ASSERT_THAT(IsTrue(AppliedDigest == CallbackDigest));
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(FrozenTick, Fixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(0, CompletedTicks));
		FGuid PostTickDigest;
		FString PostTickRootError;
		ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
			PostTickDigest, PostTickRootError)));
		ASSERT_THAT(IsTrue(AppliedDigest == PostTickDigest));

		Fixture.TickPausedFrame();
		ASSERT_THAT(AreEqual(FrozenTick + 1, Fixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(1, CompletedTicks));
	}

	TEST(PauseControlKeepsOrdinaryCommandsIsolatedAndHardPauseRejectsThem,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);

		int32 OrdinaryBatchCount = 0;
		Fixture.World->OnCommandsProcessing.AddLambda(
			[&OrdinaryBatchCount](int32, const TArray<FSeinCommand>&)
			{
				++OrdinaryBatchCount;
			});
		Fixture.World->SubmitLocalCommandDraft(
			Fixture.MakeCommand(SeinARTSTags::Command_Type_Ping));
		ASSERT_THAT(AreEqual(1, Fixture.World->GetPendingCommands().Num()));

		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		Fixture.TickPausedFrame();
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(1, Fixture.World->GetPendingCommands().Num()));
		ASSERT_THAT(AreEqual(0, OrdinaryBatchCount));

		Fixture.TickPausedFrame();
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));
		ASSERT_THAT(AreEqual(1, OrdinaryBatchCount));

		Fixture.World->FlushVisualEvents();
		Fixture.SetPausedInSim(true, /*bHard=*/true);
		Fixture.World->FlushVisualEvents();
		bool bDelegatedDuringHardPause = false;
		FSeinLocalCommandSubmitter LocalSubmitter;
		LocalSubmitter.BindLambda(
			[&bDelegatedDuringHardPause](const FSeinCommand&, bool)
			{
				bDelegatedDuringHardPause = true;
			});
		Fixture.World->SetLocalCommandSubmitter(MoveTemp(LocalSubmitter));
		Fixture.World->SubmitLocalCommandDraft(
			Fixture.MakeCommand(SeinARTSTags::Command_Type_Ping));
		Fixture.World->ClearLocalCommandSubmitter();
		ASSERT_THAT(IsFalse(bDelegatedDuringHardPause));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));
		ASSERT_THAT(IsTrue(Fixture.World->HasPendingVisualEvents()));
		Fixture.World->SubmitLocalCommandDraft(
			Fixture.MakeCommand(SeinARTSTags::Command_Type_Ping));
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));

		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		Fixture.TickPausedFrame();
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));
	}

	TEST(PauseControlPreflightRejectsInvalidFramesAtomically,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		const int32 FrozenTick = Fixture.World->GetCurrentTick();

		FSeinPauseControlFrame InvalidFrame;
		InvalidFrame.Cursor = Fixture.World->GetExpectedPauseControlCursor();
		InvalidFrame.Commands.Add(Fixture.MakeResume(FrozenTick));
		InvalidFrame.Commands.Add(Fixture.MakeCommand(
			SeinARTSTags::Command_Type_Ping, FrozenTick));
		Fixture.World->PauseControlFrameResolver.BindLambda(
			[&InvalidFrame](FSeinPauseControlFrame& OutFrame)
			{
				OutFrame = InvalidFrame;
				return true;
			});

		bool bNotified = false;
		bool bProtocolFailure = false;
		Fixture.World->PauseControlAppliedNotifier.BindLambda(
			[&](const FSeinPauseControlCursor&, bool, const FGuid& Digest, bool bFailure)
			{
				bNotified = true;
				bProtocolFailure = bFailure;
				ASSERT_THAT(IsFalse(Digest.IsValid()));
			});
		Assert.ExpectError(TEXT("Rejected pause-control frame"));
		Fixture.TickPausedFrame();
		Fixture.World->PauseControlFrameResolver.Unbind();

		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(bNotified));
		ASSERT_THAT(IsTrue(bProtocolFailure));
		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(FrozenTick, Fixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(int64(-1), Snapshot.LastAppliedPauseControlSequence));
	}

	TEST(PauseControlCapsFramesAndAcceptedStandaloneTail,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		const FSeinPauseControlCursor Cursor =
			Fixture.World->GetExpectedPauseControlCursor();

		FSeinPauseControlFrame Oversized;
		Oversized.Cursor = Cursor;
		for (int32 Index = 0;
			Index <= USeinWorldSubsystem::MaxPauseControlCommandsPerFrame;
			++Index)
		{
			Oversized.Commands.Add(Fixture.MakeResume(Cursor.FrozenTick));
		}
		Fixture.World->PauseControlFrameResolver.BindLambda(
			[&Oversized](FSeinPauseControlFrame& OutFrame)
			{
				OutFrame = Oversized;
				return true;
			});
		Assert.ExpectError(TEXT("Rejected pause-control frame"));
		Fixture.TickPausedFrame();
		Fixture.World->PauseControlFrameResolver.Unbind();
		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationPaused()));

		Fixture.World->FlushVisualEvents();
		for (int32 Index = 0;
			Index < USeinWorldSubsystem::MaxPauseControlCommandsPerFrame;
			++Index)
		{
			Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		}
		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			USeinWorldSubsystem::MaxPauseControlCommandsPerFrame,
			Snapshot.PendingStandalonePauseControlCommands.Num()));
		ASSERT_THAT(IsTrue(Fixture.World->HasPendingVisualEvents()));
	}

	TEST(PauseControlAllowsUnpauseOnlyFromFinalFrameCommand,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		const FSeinPauseControlCursor Cursor =
			Fixture.World->GetExpectedPauseControlCursor();

		FSeinPauseControlFrame InvalidOrder;
		InvalidOrder.Cursor = Cursor;
		InvalidOrder.Commands.Add(Fixture.MakeResume(Cursor.FrozenTick));
		InvalidOrder.Commands.Add(Fixture.MakeResume(Cursor.FrozenTick));
		Fixture.World->PauseControlFrameResolver.BindLambda(
			[&InvalidOrder](FSeinPauseControlFrame& OutFrame)
			{
				OutFrame = InvalidOrder;
				return true;
			});
		bool bProtocolFailure = false;
		FGuid AppliedDigest;
		Fixture.World->PauseControlAppliedNotifier.BindLambda(
			[&](const FSeinPauseControlCursor&, bool,
				const FGuid& Digest, bool bFailure)
			{
				bProtocolFailure = bFailure;
				AppliedDigest = Digest;
			});
		Assert.ExpectError(TEXT("Pause-control protocol violation"));
		Fixture.TickPausedFrame();
		Fixture.World->PauseControlFrameResolver.Unbind();
		ASSERT_THAT(IsTrue(bProtocolFailure));
		ASSERT_THAT(IsFalse(AppliedDigest.IsValid()));
		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(int64(0),
			Fixture.World->GetExpectedPauseControlCursor().Sequence));

		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		Fixture.TickPausedFrame();
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));
	}

	TEST(PauseControlEpochsRejectStaleFramesAndRestartTheirSequence,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		const FSeinPauseControlCursor FirstCursor =
			Fixture.World->GetExpectedPauseControlCursor();
		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		Fixture.TickPausedFrame();
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));

		Fixture.SetPausedInSim(true);
		const FSeinPauseControlCursor SecondCursor =
			Fixture.World->GetExpectedPauseControlCursor();
		ASSERT_THAT(AreEqual(FirstCursor.PauseEpoch + 1, SecondCursor.PauseEpoch));
		ASSERT_THAT(AreEqual(int64(0), SecondCursor.Sequence));

		FSeinPauseControlFrame StaleFrame;
		StaleFrame.Cursor = FirstCursor;
		StaleFrame.Commands.Add(Fixture.MakeResume(SecondCursor.FrozenTick));
		Fixture.World->PauseControlFrameResolver.BindLambda(
			[&StaleFrame](FSeinPauseControlFrame& OutFrame)
			{
				OutFrame = StaleFrame;
				return true;
			});
		Assert.ExpectError(TEXT("Rejected pause-control frame"));
		Fixture.TickPausedFrame();
		Fixture.World->PauseControlFrameResolver.Unbind();
		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationPaused()));

		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());
		Fixture.TickPausedFrame();
		FSeinWorldSnapshot Applied;
		Fixture.World->CaptureSnapshot(Applied);
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(SecondCursor.PauseEpoch, Applied.PauseEpoch));
		ASSERT_THAT(AreEqual(int64(0), Applied.LastAppliedPauseControlSequence));
	}

	TEST(PauseControlCursorRoundTripsThroughSnapshotAndStateHash,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		const FSeinPauseControlCursor Cursor =
			Fixture.World->GetExpectedPauseControlCursor();

		FSeinPauseControlFrame RejectedButCanonical;
		RejectedButCanonical.Cursor = Cursor;
		FSeinCommand Unauthorized = Fixture.MakeResume(Cursor.FrozenTick);
		Unauthorized.PlayerID = FSeinPlayerID(2);
		RejectedButCanonical.Commands.Add(Unauthorized);
		Fixture.World->PauseControlFrameResolver.BindLambda(
			[&RejectedButCanonical](FSeinPauseControlFrame& OutFrame)
			{
				OutFrame = RejectedButCanonical;
				return true;
			});
		FGuid RejectedFrameDigest;
		bool bRejectedFrameProtocolFailure = true;
		Fixture.World->PauseControlAppliedNotifier.BindLambda(
			[&](const FSeinPauseControlCursor&, bool,
				const FGuid& Digest, bool bFailure)
			{
				RejectedFrameDigest = Digest;
				bRejectedFrameProtocolFailure = bFailure;
			});
		Fixture.TickPausedFrame();
		Fixture.World->PauseControlFrameResolver.Unbind();
		Fixture.World->PauseControlAppliedNotifier.Unbind();
		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(IsFalse(bRejectedFrameProtocolFailure));
		ASSERT_THAT(IsTrue(RejectedFrameDigest.IsValid()));

		FSeinWorldSnapshot Captured;
		Fixture.World->CaptureSnapshot(Captured);
		const int32 CapturedHash = Fixture.World->ComputeStateHash();
		ASSERT_THAT(AreEqual(Cursor.PauseEpoch, Captured.PauseEpoch));
		ASSERT_THAT(AreEqual(Cursor.FrozenTick, Captured.PauseFrozenTick));
		ASSERT_THAT(AreEqual(int64(0), Captured.LastAppliedPauseControlSequence));

		Fixture.SetPausedInSim(false);
		ASSERT_THAT(IsTrue(CapturedHash != Fixture.World->ComputeStateHash()));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Captured)));
		ASSERT_THAT(IsTrue(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(CapturedHash, Fixture.World->ComputeStateHash()));
		ASSERT_THAT(AreEqual(int64(1),
			Fixture.World->GetExpectedPauseControlCursor().Sequence));
	}

	TEST(PauseControlReportsCanonicalCaptureRefusalWithoutReplayingFrame,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		const FSeinPauseControlCursor Cursor =
			Fixture.World->GetExpectedPauseControlCursor();

		FSeinPauseControlFrame Frame;
		Frame.Cursor = Cursor;
		FSeinCommand Unauthorized = Fixture.MakeResume(Cursor.FrozenTick);
		Unauthorized.PlayerID = FSeinPlayerID(2);
		Frame.Commands.Add(Unauthorized);
		Fixture.World->PauseControlFrameResolver.BindLambda(
			[&Frame](FSeinPauseControlFrame& OutFrame)
			{
				OutFrame = Frame;
				return true;
			});

		FString ReplayError;
		ASSERT_THAT(IsTrue(
			Fixture.World->BeginReplayExclusiveCommandIngressForTests(
				ReplayError)));
		int32 NotificationCount = 0;
		bool bProtocolFailure = true;
		FGuid AppliedDigest;
		Fixture.World->PauseControlAppliedNotifier.BindLambda(
			[&](const FSeinPauseControlCursor&, bool,
				const FGuid& Digest, bool bFailure)
			{
				++NotificationCount;
				AppliedDigest = Digest;
				bProtocolFailure = bFailure;
			});

		Assert.ExpectError(TEXT(
			"applied, but canonical state-root capture failed"));
		Fixture.TickPausedFrame();
		Fixture.World->EndReplayExclusiveCommandIngressForTests();
		Fixture.World->PauseControlFrameResolver.Unbind();
		Fixture.World->PauseControlAppliedNotifier.Unbind();

		ASSERT_THAT(AreEqual(1, NotificationCount));
		ASSERT_THAT(IsFalse(bProtocolFailure));
		ASSERT_THAT(IsFalse(AppliedDigest.IsValid()));
		ASSERT_THAT(AreEqual(
			Cursor.Sequence + 1,
			Fixture.World->GetExpectedPauseControlCursor().Sequence));
	}

	TEST(PauseSnapshotRestoresExactOrdinaryAndStandaloneCommandTails,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		Fixture.World->SubmitLocalCommandDraft(
			Fixture.MakeCommand(SeinARTSTags::Command_Type_Ping));
		Fixture.World->SubmitLocalCommandDraft(Fixture.MakeResume());

		FSeinWorldSnapshot Captured;
		Fixture.World->CaptureSnapshot(Captured);
		const int32 CapturedHash = Fixture.World->ComputeStateHash();
		ASSERT_THAT(AreEqual(1, Captured.PendingCommands.Num()));
		ASSERT_THAT(AreEqual(
			1, Captured.PendingStandalonePauseControlCommands.Num()));

		Fixture.SetPausedInSim(false);
		Fixture.World->SubmitLocalCommandDraft(
			Fixture.MakeCommand(SeinARTSTags::Command_Type_Ping));
		ASSERT_THAT(IsTrue(CapturedHash != Fixture.World->ComputeStateHash()));

		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, Captured)));
		FSeinWorldSnapshot Restored;
		Fixture.World->CaptureSnapshot(Restored);
		ASSERT_THAT(AreEqual(CapturedHash, Fixture.World->ComputeStateHash()));
		ASSERT_THAT(AreEqual(1, Restored.PendingCommands.Num()));
		ASSERT_THAT(AreEqual(
			1, Restored.PendingStandalonePauseControlCommands.Num()));

		Fixture.TickPausedFrame();
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));
		ASSERT_THAT(AreEqual(1, Fixture.World->GetPendingCommands().Num()));
		Fixture.TickPausedFrame();
		ASSERT_THAT(AreEqual(0, Fixture.World->GetPendingCommands().Num()));
	}

	TEST(PauseSnapshotRejectsImpossibleMatchPausePairsWithoutMutation,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		Fixture.SetPausedInSim(true);
		FSeinWorldSnapshot Valid;
		Fixture.World->CaptureSnapshot(Valid);
		const int32 HashBefore = Fixture.World->ComputeStateHash();

		FSeinWorldSnapshot PlayingButFrozen = Valid;
		PlayingButFrozen.MatchState = static_cast<uint8>(ESeinMatchState::Playing);
		Assert.ExpectError(TEXT("RestoreSnapshot: invalid match/pause state."));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, PlayingButFrozen)));
		ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));

		FSeinWorldSnapshot PausedButAdvancing = Valid;
		PausedButAdvancing.bSimPaused = false;
		PausedButAdvancing.bSimPausedHard = false;
		Assert.ExpectError(TEXT("RestoreSnapshot: invalid match/pause state."));
		ASSERT_THAT(IsFalse(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Fixture.World, PausedButAdvancing)));
		ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
	}

	TEST(RunningSimulationRejectsPresentationSideDirectMatchMutation,
		"SeinARTS.Unit.Authority.PauseControl")
	{
		FRunningMatchFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));

		Assert.ExpectError(TEXT(
			"SetSimPaused rejected outside bootstrap Applying or deterministic simulation context"));
		Fixture.World->SetSimPaused(true);
		ASSERT_THAT(IsFalse(Fixture.World->IsSimulationPaused()));

		Assert.ExpectError(TEXT(
			"EndMatch rejected outside bootstrap Applying or deterministic simulation context"));
		Fixture.World->EndMatch(FSeinPlayerID::Neutral(), FGameplayTag());
		ASSERT_THAT(AreEqual(ESeinMatchState::Playing, Fixture.World->GetMatchState()));

		FActorTestSpawner LobbySpawner;
		USeinWorldSubsystem* LobbyWorld =
			LobbySpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(LobbyWorld));
		Assert.ExpectError(TEXT(
			"Simulation start requires a valid frozen execution topology"));
		ASSERT_THAT(IsFalse(LobbyWorld->StartSimulation()));
		ASSERT_THAT(AreEqual(ESeinMatchState::Lobby, LobbyWorld->GetMatchState()));
	}
}
