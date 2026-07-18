#include "CQTest.h"
#include "SeinNetSubsystem.h"
#include "Engine/GameInstance.h"

struct FSeinNetSubsystemTestAccess
{
	static USeinNetSubsystem* NewSubsystem()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>();
		return NewObject<USeinNetSubsystem>(GameInstance);
	}

	static int32 InputDelay(const USeinNetSubsystem& Net) { return Net.GetInputDelayTurns(); }
	static int32 TicksPerTurn(const USeinNetSubsystem& Net) { return Net.GetTicksPerTurn(); }
	static void Buffer(USeinNetSubsystem& Net, const FSeinCommand& Command) { Net.PendingOutgoingCommands.Add(Command); }
	static void QueueThrough(USeinNetSubsystem& Net, int32 Turn, bool bAttachCommands)
	{
		Net.QueueTurnSubmissionsThrough(Turn, bAttachCommands);
	}
	static void FlushTurns(USeinNetSubsystem& Net) { Net.FlushPendingTurnSubmissions(); }
	static int32 PendingTurnCount(const USeinNetSubsystem& Net) { return Net.PendingTurnSubmissions.Num(); }
	static int32 PendingCommandCount(const USeinNetSubsystem& Net) { return Net.PendingOutgoingCommands.Num(); }
	static int32 FrozenCommandTick(const USeinNetSubsystem& Net)
	{
		return Net.PendingTurnSubmissions[0].Commands[0].Tick;
	}
	static int32 LastSubmittedTurn(const USeinNetSubsystem& Net) { return Net.LastSubmittedTurn; }

	static void SetTurnTransport(USeinNetSubsystem& Net,
		TFunction<bool(int32, const TArray<FSeinCommand>&)> Transport)
	{
		Net.TestTurnSubmitOverride = MoveTemp(Transport);
	}

	static void EnqueueHash(USeinNetSubsystem& Net, int32 Turn, int32 Hash)
	{
		Net.EnqueueStateHashReport(Turn, Hash);
	}
	static void FlushHashes(USeinNetSubsystem& Net) { Net.FlushPendingStateHashReports(); }
	static int32 PendingHashCount(const USeinNetSubsystem& Net) { return Net.PendingStateHashReports.Num(); }
	static int32 LastHashReported(const USeinNetSubsystem& Net) { return Net.LastHashReportedTurn; }
	static void SetHashTransport(USeinNetSubsystem& Net, TFunction<bool(int32, int32)> Transport)
	{
		Net.TestHashSubmitOverride = MoveTemp(Transport);
	}

	static void SeedEpochState(USeinNetSubsystem& Net)
	{
		Net.ServerTurnBuffer.FindOrAdd(7);
		Net.CompletedTurns.Add(6);
		Net.CompletedTurnRejectionFloor = 2;
		Net.ReceivedTurns.FindOrAdd(8);
		Net.PendingOutgoingCommands.AddDefaulted();
		FSeinPendingTurnSubmission& Submission = Net.PendingTurnSubmissions.Emplace_GetRef();
		Submission.TurnId = 9;
		Net.LastQueuedTurn = 9;
		Net.LastSubmittedTurn = 8;
		Net.bStartSessionRequested = true;
		Net.bServerStartRequested = true;

		Net.ServerHashReports.FindOrAdd(10);
		Net.CompletedHashChecks.Add(5);
		Net.CompletedHashRejectionFloor = 1;
		Net.PendingStateHashReports.Add({11, 1234});
		Net.LastHashQueuedTurn = 11;
		Net.LastHashReportedTurn = 10;
		Net.PendingAICommands.FindOrAdd(FSeinPlayerID(1)).AddDefaulted();
		Net.IncompleteTurnDiagnostics.Add(7, {1.0, 1.0, false});

		// Deliberately match-scoped: the epoch reset must not guess these
		// seamless-travel semantics before Gate A.
		Net.SessionSeed = 77;
		Net.bDesyncDetected = true;
		Net.SlotLifecycle.Add(FSeinPlayerID(1), ESeinSlotLifecycle::Connected);
	}

	static void ResetEpoch(USeinNetSubsystem& Net) { Net.ResetLockstepEpochState(nullptr); }
	static bool IsEpochStateClear(const USeinNetSubsystem& Net)
	{
		return Net.ServerTurnBuffer.IsEmpty() && Net.CompletedTurns.IsEmpty() &&
			Net.CompletedTurnRejectionFloor == -1 && Net.ReceivedTurns.IsEmpty() &&
			Net.PendingOutgoingCommands.IsEmpty() && Net.PendingTurnSubmissions.IsEmpty() &&
			Net.LastQueuedTurn == -1 && Net.LastSubmittedTurn == -1 && !Net.bStartSessionRequested &&
			!Net.bServerStartRequested &&
			Net.ServerHashReports.IsEmpty() && Net.CompletedHashChecks.IsEmpty() &&
			Net.CompletedHashRejectionFloor == -1 && Net.PendingStateHashReports.IsEmpty() &&
			Net.LastHashQueuedTurn == -1 && Net.LastHashReportedTurn == -1 &&
			Net.PendingAICommands.IsEmpty() && Net.AITakeoverControllers.IsEmpty() &&
			Net.IncompleteTurnDiagnostics.IsEmpty();
	}
	static bool MatchStateWasPreserved(const USeinNetSubsystem& Net)
	{
		return Net.SessionSeed == 77 && Net.bDesyncDetected &&
			Net.SlotLifecycle.Contains(FSeinPlayerID(1));
	}
	static bool AcceptsCommandTurn(const USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.IsCommandTurnWithinProtocolWindow(Turn, TEXT("Test"));
	}
	static void SeedAndPruneCompletedHistory(USeinNetSubsystem& Net, int32 ReferenceTurn)
	{
		for (int32 Turn = 0; Turn <= ReferenceTurn; ++Turn)
		{
			Net.CompletedTurns.Add(Turn);
			Net.CompletedHashChecks.Add(Turn);
		}
		Net.PruneProtocolState(ReferenceTurn);
	}
	static int32 CompletedTurnCount(const USeinNetSubsystem& Net) { return Net.CompletedTurns.Num(); }
	static int32 CompletedHashCount(const USeinNetSubsystem& Net) { return Net.CompletedHashChecks.Num(); }
	static int32 TurnFloor(const USeinNetSubsystem& Net) { return Net.CompletedTurnRejectionFloor; }
	static int32 HashFloor(const USeinNetSubsystem& Net) { return Net.CompletedHashRejectionFloor; }

	static void SetDedicatedAuthority(USeinNetSubsystem& Net, bool bDedicated)
	{
		Net.TestDedicatedAuthorityOverride = bDedicated;
	}
	static void SetSessionSeed(USeinNetSubsystem& Net, int64 Seed) { Net.SessionSeed = Seed; }
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

	static bool ExpectedCommandSlotsComplete(
		const TArray<FSeinPlayerID>& Expected,
		const TMap<FSeinPlayerID, TArray<FSeinCommand>>& Submissions)
	{
		return USeinNetSubsystem::AreExpectedCommandSlotsComplete(Expected, Submissions);
	}
	static int32 BufferCommandFirstWins(
		USeinNetSubsystem& Net, int32 Turn, FSeinPlayerID Slot, TArray<FSeinCommand> Commands)
	{
		return static_cast<int32>(Net.BufferCommandSubmissionFirstWins(
			Turn, Slot, MoveTemp(Commands)));
	}
	static int32 StoredCommandTick(const USeinNetSubsystem& Net, int32 Turn, FSeinPlayerID Slot)
	{
		return Net.ServerTurnBuffer.FindChecked(Turn).FindChecked(Slot)[0].Tick;
	}
	static int32 BufferHashFirstWins(
		USeinNetSubsystem& Net, int32 Turn, FSeinPlayerID Slot, int32 Hash)
	{
		return static_cast<int32>(Net.BufferHashReportFirstWins(Turn, Slot, Hash));
	}
	static int32 StoredHash(const USeinNetSubsystem& Net, int32 Turn, FSeinPlayerID Slot)
	{
		return Net.ServerHashReports.FindChecked(Turn).FindChecked(Slot);
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
		const TArray<FSeinPlayerID>& Expected,
		const TMap<FSeinPlayerID, int32>& Accepted,
		int32 RequiredFingerprint)
	{
		return USeinNetSubsystem::AreConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint);
	}
	static void BufferAICommand(
		USeinNetSubsystem& Net, FSeinPlayerID Slot, const FSeinCommand& Command)
	{
		Net.PendingAICommands.FindOrAdd(Slot).Add(Command);
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
		const TArray<FSeinCommand>* Pending = Net.PendingAICommands.Find(Slot);
		return Pending ? Pending->Num() : 0;
	}
	static void MarkTurnIncomplete(USeinNetSubsystem& Net, int32 Turn)
	{
		Net.IncompleteTurnDiagnostics.Add(Turn, {1.0, 1.0, false});
	}
	static void FinalizeTurnDiagnostics(
		USeinNetSubsystem& Net, int32 Turn, FSeinPlayerID CompletingSubmitter)
	{
		Net.FinalizeCompletedTurnDiagnostics(Turn, CompletingSubmitter);
	}
	static bool HasIncompleteTurn(const USeinNetSubsystem& Net, int32 Turn)
	{
		return Net.IncompleteTurnDiagnostics.Contains(Turn);
	}
	static int32 StragglerCount(const USeinNetSubsystem& Net, FSeinPlayerID Slot)
	{
		return Net.StragglerCounts.FindRef(Slot);
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

		FSeinCommand First;
		First.Tick = 111;
		FSeinCommand Later;
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

	TEST(PendingHashRetriesCapturedCheckpoint, "SeinARTS.Unit.Network")
	{
		TestRunner->AddExpectedError(
			TEXT("FlushPendingStateHashReports: retaining exact turn=10 hash=0x12345678 for retry."),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));

		FSeinNetSubsystemTestAccess::EnqueueHash(*Net, 10, 0x12345678);
		FSeinNetSubsystemTestAccess::SetHashTransport(*Net,
			[](int32, int32) { return false; });
		FSeinNetSubsystemTestAccess::FlushHashes(*Net);
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::PendingHashCount(*Net)));
		ASSERT_THAT(AreEqual(-1, FSeinNetSubsystemTestAccess::LastHashReported(*Net)));

		int32 SentTurn = INDEX_NONE;
		int32 SentHash = 0;
		FSeinNetSubsystemTestAccess::SetHashTransport(*Net,
			[&](int32 Turn, int32 Hash)
			{
				SentTurn = Turn;
				SentHash = Hash;
				return true;
			});
		FSeinNetSubsystemTestAccess::FlushHashes(*Net);
		ASSERT_THAT(AreEqual(10, SentTurn));
		ASSERT_THAT(AreEqual(0x12345678, SentHash));
		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::PendingHashCount(*Net)));
		ASSERT_THAT(AreEqual(10, FSeinNetSubsystemTestAccess::LastHashReported(*Net)));
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

	TEST(CompletedProtocolHistoryIsBounded, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		FSeinNetSubsystemTestAccess::SeedAndPruneCompletedHistory(*Net, 600);

		ASSERT_THAT(AreEqual(256, FSeinNetSubsystemTestAccess::CompletedTurnCount(*Net)));
		ASSERT_THAT(AreEqual(256, FSeinNetSubsystemTestAccess::CompletedHashCount(*Net)));
		ASSERT_THAT(AreEqual(344, FSeinNetSubsystemTestAccess::TurnFloor(*Net)));
		ASSERT_THAT(AreEqual(344, FSeinNetSubsystemTestAccess::HashFloor(*Net)));
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

	TEST(TurnCompletionRequiresEveryCanonicalExpectedSlot, "SeinARTS.Unit.Network")
	{
		const FSeinPlayerID LiveA(1);
		const FSeinPlayerID LiveB(2);
		const FSeinPlayerID ExtraDropped(3);
		const TArray<FSeinPlayerID> Expected{LiveA, LiveB};
		TMap<FSeinPlayerID, TArray<FSeinCommand>> Submissions;
		Submissions.Add(LiveA);
		Submissions.Add(ExtraDropped);

		// Matching cardinality is insufficient: an extra dropped/AI heartbeat
		// must never stand in for a missing live slot ID.
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ExpectedCommandSlotsComplete(
			Expected, Submissions)));
		Submissions.Add(LiveB);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::ExpectedCommandSlotsComplete(
			Expected, Submissions)));
	}

	TEST(CommandAndHashDuplicatesAreFirstAcceptedWins, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinPlayerID Slot(1);

		FSeinCommand Original;
		Original.Tick = 111;
		FSeinCommand Conflict = Original;
		Conflict.Tick = 222;
		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::BufferCommandFirstWins(
			*Net, 7, Slot, {Original})));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::BufferCommandFirstWins(
			*Net, 7, Slot, {Original})));
		ASSERT_THAT(AreEqual(2, FSeinNetSubsystemTestAccess::BufferCommandFirstWins(
			*Net, 7, Slot, {Conflict})));
		ASSERT_THAT(AreEqual(111, FSeinNetSubsystemTestAccess::StoredCommandTick(*Net, 7, Slot)));

		ASSERT_THAT(AreEqual(0, FSeinNetSubsystemTestAccess::BufferHashFirstWins(
			*Net, 10, Slot, 0x11111111)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::BufferHashFirstWins(
			*Net, 10, Slot, 0x11111111)));
		ASSERT_THAT(AreEqual(2, FSeinNetSubsystemTestAccess::BufferHashFirstWins(
			*Net, 10, Slot, 0x22222222)));
		ASSERT_THAT(AreEqual(0x11111111, FSeinNetSubsystemTestAccess::StoredHash(*Net, 10, Slot)));
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
		const FSeinPlayerID SlotA(1);
		const FSeinPlayerID SlotB(2);
		const int32 RequiredFingerprint = 0x13572468;
		const TArray<FSeinPlayerID> Expected{SlotA, SlotB};
		TMap<FSeinPlayerID, int32> Accepted;

		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
		Accepted.Add(SlotA, RequiredFingerprint);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
		// Logout/simulated disconnect removes SlotB from the connected expected
		// set, so the same deferred request becomes eligible immediately.
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			{SlotA}, Accepted, RequiredFingerprint)));

		// A received but rejected/stale report cannot satisfy the second slot.
		Accepted.Add(SlotB, 0x24681357);
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
		Accepted.Add(SlotB, RequiredFingerprint);
		ASSERT_THAT(IsTrue(FSeinNetSubsystemTestAccess::ConfigFingerprintsComplete(
			Expected, Accepted, RequiredFingerprint)));
	}

	TEST(AITakeoverDrainAuthorityStampsSlotAndTurnTick, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinPlayerID OwnedSlot(4);
		const int32 Turn = 9;

		FSeinCommand Forged;
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
			Turn * FSeinNetSubsystemTestAccess::TicksPerTurn(*Net), Drained[0].Tick));
		ASSERT_THAT(IsFalse(FSeinNetSubsystemTestAccess::BuildDroppedSubmission(
			*Net, OwnedSlot, Turn + 1, true, Drained)));
	}

	TEST(InterleavedIncompleteTurnsPreserveActualCompletingSubmitters, "SeinARTS.Unit.Network")
	{
		USeinNetSubsystem* Net = FSeinNetSubsystemTestAccess::NewSubsystem();
		ASSERT_THAT(IsNotNull(Net));
		const FSeinPlayerID SlotA(1);
		const FSeinPlayerID SlotB(2);

		FSeinNetSubsystemTestAccess::MarkTurnIncomplete(*Net, 7);
		FSeinNetSubsystemTestAccess::MarkTurnIncomplete(*Net, 8);
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

		// An ordinarily completed turn increments the denominator without
		// inventing another straggler event.
		FSeinNetSubsystemTestAccess::FinalizeTurnDiagnostics(*Net, 9, SlotA);
		ASSERT_THAT(AreEqual(3, Net->GetTurnsCompletedCount()));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotA)));
		ASSERT_THAT(AreEqual(1, FSeinNetSubsystemTestAccess::StragglerCount(*Net, SlotB)));
	}
}
