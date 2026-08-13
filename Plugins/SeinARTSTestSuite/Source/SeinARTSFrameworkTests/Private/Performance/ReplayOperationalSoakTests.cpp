/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         ReplayOperationalSoakTests.cpp
 * @author       RJ Macklem
 * @created      13 Aug 2026
 * @latest       13 Aug 2026
 * @brief        Qualifies sustained replay checkpoint I/O, GC overlap,
 *               process-memory bounds, latency tails, and capacity failure.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "Data/SeinRelationshipTypes.h"
#include "Data/SeinReplayHeader.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/MemoryMisc.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "SeinReplayFormat.h"
#include "SeinReplayReader.h"
#include "SeinReplayWriter.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinComponentStorageTestTypes.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectArray.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool TickSimulation(USeinWorldSubsystem& World, float DeltaTime)
	{
		return World.TickSimulation(DeltaTime);
	}
};

namespace UE::SeinARTSTests
{
	namespace ReplayOperationalSoakTestLocal
	{
		constexpr uint64 MiB = 1024ULL * 1024ULL;

		struct FScopedReplaySettings
		{
			FScopedReplaySettings(
				int32 CheckpointIntervalTurns,
				int32 TurnBatchSize,
				int32 MaximumFileSizeMiB)
			{
				Settings = GetMutableDefault<USeinARTSCoreSettings>();
				check(Settings);
				PreviousCheckpointInterval =
					Settings->ReplayCheckpointIntervalTurns;
				PreviousTurnBatchSize = Settings->ReplayTurnBatchSize;
				PreviousMaximumFileSizeMiB = Settings->ReplayMaxFileSizeMiB;
				Settings->ReplayCheckpointIntervalTurns =
					CheckpointIntervalTurns;
				Settings->ReplayTurnBatchSize = TurnBatchSize;
				Settings->ReplayMaxFileSizeMiB = MaximumFileSizeMiB;
			}

			~FScopedReplaySettings()
			{
				Settings->ReplayCheckpointIntervalTurns =
					PreviousCheckpointInterval;
				Settings->ReplayTurnBatchSize = PreviousTurnBatchSize;
				Settings->ReplayMaxFileSizeMiB =
					PreviousMaximumFileSizeMiB;
			}

			USeinARTSCoreSettings* Settings = nullptr;
			int32 PreviousCheckpointInterval = 0;
			int32 PreviousTurnBatchSize = 0;
			int32 PreviousMaximumFileSizeMiB = 0;
		};

		struct FScopedReplayFile
		{
			FString Path;

			~FScopedReplayFile()
			{
				if (!Path.IsEmpty()
					&& IFileManager::Get().FileExists(*Path)
					&& !IFileManager::Get().Delete(*Path, false, true))
				{
					UE_LOG(LogTemp, Error,
						TEXT("Could not delete replay soak artifact: %s"),
						*Path);
				}
			}
		};

		struct FScopedReplayWorkerDrain
		{
			USeinReplayWriter* Writer = nullptr;

			~FScopedReplayWorkerDrain()
			{
				if (Writer)
				{
					Writer->AbortAndDrainBackgroundWorkForTests();
				}
			}
		};

		bool PumpGameThreadTasksUntil(
			TFunctionRef<bool()> IsComplete,
			double TimeoutSeconds = 10.0)
		{
			const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
			do
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(
					ENamedThreads::GameThread);
				if (IsComplete())
				{
					return true;
				}
				FPlatformProcess::SleepNoStats(0.001f);
			}
			while (FPlatformTime::Seconds() < Deadline);

			FTaskGraphInterface::Get().ProcessThreadUntilIdle(
				ENamedThreads::GameThread);
			return IsComplete();
		}

		FSeinMatchSettings MakeTwoPlayerMatchSettings()
		{
			FSeinMatchSettings Settings;
			for (int32 SlotIndex = 1; SlotIndex <= 2; ++SlotIndex)
			{
				FSeinMatchSlot& Slot = Settings.Slots.AddDefaulted_GetRef();
				Slot.SlotIndex = SlotIndex;
				Slot.State = ESeinSlotState::Human;
			}
			return Settings;
		}

		FSeinReplayHeader MakePreparedWorldHeader(USeinWorldSubsystem& World)
		{
			FSeinReplayHeader Header;
			SeinReplayCompatibility::StampCurrent(Header, World.GetWorld());
			Header.CommandProtocolDigest = World.GetCommandProtocolDigest();
			Header.MatchSettingsDigest = World.GetMatchSettingsDigest();
			Header.BootstrapReceipt = World.GetMatchBootstrapReceipt();
			Header.ConfigFingerprint = World.GetConfigFingerprint();
			Header.RandomSeed = World.GetSessionSeed();
			Header.SettingsSnapshot = World.GetMatchSettings();
			Header.StartTick = World.GetCurrentTick();
			Header.RecordedAt = FDateTime::UtcNow();
			for (const FSeinMatchSlot& Slot : Header.SettingsSnapshot.Slots)
			{
				if (Slot.State != ESeinSlotState::Human
					&& Slot.State != ESeinSlotState::AI)
				{
					continue;
				}
				FSeinPlayerRegistration& Player =
					Header.Players.AddDefaulted_GetRef();
				Player.PlayerID = FSeinPlayerID(
					static_cast<uint8>(Slot.SlotIndex));
				Player.FactionID = Slot.FactionID;
				Player.TeamID = Slot.TeamID;
				Player.bIsAI = Slot.State == ESeinSlotState::AI;
			}
			return Header;
		}

		FSeinCommand MakeCapabilityMutation(int32 Tick, bool bGrant)
		{
			FSeinSetPairCapabilityCommandPayload Payload;
			Payload.SourcePlayer = FSeinPlayerID(1);
			Payload.TargetPlayer = FSeinPlayerID(2);
			Payload.CapabilityTag =
				SeinARTSTags::Relationship_Capability_ShareVision;
			Payload.SourceKindTag =
				SeinARTSTags::Relationship_Source_TeamBootstrap;
			Payload.SourceInstanceID = 0x534F414B;
			Payload.bGrant = bGrant;

			FSeinCommand Command;
			Command.CommandType =
				SeinARTSTags::Command_Type_SetPairCapability;
			Command.SchemaVersion = 1;
			Command.Tick = Tick;
			Command.IssuerKind =
				ESeinCommandIssuerKind::MatchAdministrator;
			Command.Payload = FInstancedStruct::Make(Payload);
			return Command;
		}

		USeinReplayWriter* StartRecording(
			USeinWorldSubsystem& World,
			const FSeinMatchSettings& MatchSettings,
			int32 Population,
			FString& OutError)
		{
			bool bAuthoringSucceeded = true;
			const auto AuthorState = [&]()
			{
				for (const FSeinMatchSlot& Slot : MatchSettings.Slots)
				{
					if (Slot.State != ESeinSlotState::Human
						&& Slot.State != ESeinSlotState::AI)
					{
						continue;
					}
					World.RegisterPlayer(
						FSeinPlayerID(static_cast<uint8>(Slot.SlotIndex)),
						Slot.FactionID,
						Slot.TeamID);
				}
				for (int32 Index = 0; Index < Population; ++Index)
				{
					const FSeinEntityHandle Handle = World.SpawnAbstractEntity(
						FFixedTransform(FFixedVector(
							FFixedPoint::FromInt((Index % 32) * 100),
							FFixedPoint::FromInt((Index / 32) * 100),
							FFixedPoint::Zero)),
						FSeinPlayerID::Neutral());
					if (!Handle.IsValid())
					{
						bAuthoringSucceeded = false;
						OutError = TEXT("Could not spawn the replay soak population.");
						return;
					}

					FSeinComponentStorageLifecycleProbe Probe;
					Probe.Values = {Index, Index * 3, Index % 11};
					World.AddComponent(Handle, Probe);
				}
			};
			if (!SeinTestMatchBootstrap::Materialize(
					World,
					AuthorState,
					MatchSettings,
					/*SessionSeed=*/0,
					FName(TEXT("SeinFrameworkTests.ReplayOperationalSoak")),
					&OutError)
				|| !bAuthoringSucceeded
				|| !SeinTestMatchBootstrap::Authorize(World, &OutError))
			{
				return nullptr;
			}

			USeinReplayWriter* Writer = NewObject<USeinReplayWriter>(&World);
			Writer->StartRecording(MakePreparedWorldHeader(World));
			if (!Writer->IsRecording()
				|| !SeinTestMatchBootstrap::Start(World, &OutError)
				|| !Writer->CaptureCheckpoint(/*bRequired=*/true))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("Could not start the replay soak recording.");
				}
				return nullptr;
			}
			World.StopSimulation();
			return Writer;
		}

		bool AdvanceTurn(
			USeinWorldSubsystem& World,
			USeinReplayWriter& Writer,
			int32 Turn,
			int32 TicksPerTurn,
			bool bGrant)
		{
			const int32 TurnTick = Turn * TicksPerTurn;
			const FSeinCommand Mutation =
				MakeCapabilityMutation(TurnTick, bGrant);
			Writer.RecordTurn(Turn, {Mutation});
			if (!Writer.IsRecording())
			{
				return false;
			}
			while (World.GetCurrentTick() < TurnTick)
			{
				const int32 ExpectedTick = World.GetCurrentTick() + 1;
				if (ExpectedTick == TurnTick)
				{
					World.SubmitLocalCommandDraft(
						Mutation, /*bRequestMatchAdministration=*/true);
				}
				if (!FSeinWorldSubsystemTestAccess::TickSimulation(
						World,
						World.GetFixedDeltaTimeSeconds())
					|| World.GetCurrentTick() != ExpectedTick)
				{
					return false;
				}
				Writer.ObserveCompletedTick(ExpectedTick);
			}
			return World.HasPairCapability(
				FSeinPlayerID(1),
				FSeinPlayerID(2),
				SeinARTSTags::Relationship_Capability_ShareVision) == bGrant;
		}

		double Percentile(TArray<double> Samples, double Fraction)
		{
			check(!Samples.IsEmpty());
			Samples.Sort();
			const int32 Index = FMath::Clamp(
				FMath::CeilToInt(Fraction * Samples.Num()) - 1,
				0,
				Samples.Num() - 1);
			return Samples[Index];
		}

		uint64 PositiveDelta(uint64 Value, uint64 Baseline)
		{
			return Value > Baseline ? Value - Baseline : 0;
		}

	}

	TEST(ReplayOperationalSoakKeepsWorkersMemoryAndLatencyBounded,
		"SeinARTS.Perf.Replay.OperationalSoak")
	{
		using namespace ReplayOperationalSoakTestLocal;
		constexpr int32 Population = 128;
		constexpr int32 CheckpointIntervalTurns = 7;
		constexpr int32 PeriodicCheckpointCount = 64;
		constexpr int32 PeriodicTurns =
			CheckpointIntervalTurns * PeriodicCheckpointCount;
		// Leave one grant command beyond the final periodic checkpoint so full
		// playback must apply journal catch-up instead of only restoring a snapshot.
		constexpr int32 TotalTurns = PeriodicTurns + 1;
		constexpr int32 GcIntervalCheckpoints = 8;
		constexpr int32 SampleIntervalCheckpoints = 7;
		constexpr int32 MaximumFileSizeMiB = 512;
		constexpr uint64 MaximumWorkingSetGrowthBytes = 256 * MiB;
		constexpr uint64 MaximumCommitGrowthBytes = 128 * MiB;
		constexpr uint64 MaximumPeakGrowthBytes = 768 * MiB;
		constexpr uint64 MaximumLateGrowthBytes = 128 * MiB;
		constexpr double MaximumP95LatencyMilliseconds = 1000.0;
		constexpr double MaximumLatencyMilliseconds = 5000.0;

		FScopedReplaySettings ReplaySettings(
			CheckpointIntervalTurns,
			CheckpointIntervalTurns,
			MaximumFileSizeMiB);
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FString Error;
		TStrongObjectPtr<USeinReplayWriter> Writer(StartRecording(
			*Source,
			MakeTwoPlayerMatchSettings(),
			Population,
			Error));
		if (!Writer.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("Replay operational soak setup failed: %s"), *Error);
		}
		ASSERT_THAT(IsTrue(Writer.IsValid()));
		if (!Writer.IsValid())
		{
			return;
		}
		FScopedReplayFile ReplayFile{Writer->GetActivePartialPath()};
		FScopedReplayWorkerDrain WorkerDrain{Writer.Get()};
		ASSERT_THAT(AreEqual(1, Writer->GetPersistedCheckpointCount()));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
		const int32 FirstTurn = FMath::Max(1, Settings->InputDelayTurns);
		const uint64 StableCacheBytes =
			Source->GetComponentStorageSnapshotCacheBytesForTests();
		const int64 InitialCacheHits =
			Source->GetComponentStorageSnapshotCacheHitCountForTests();
		ASSERT_THAT(IsTrue(StableCacheBytes > 0));

		CollectGarbage(RF_NoFlags);
		FMemory::Trim(/*bTrimThreadCaches=*/true);
		const FPlatformMemoryStats ColdMemory = FPlatformMemory::GetStats();
		constexpr int32 ControlGcCount =
			PeriodicCheckpointCount / GcIntervalCheckpoints;
		for (int32 ControlGc = 0; ControlGc < ControlGcCount; ++ControlGc)
		{
			CollectGarbage(RF_NoFlags);
		}
		FMemory::Trim(/*bTrimThreadCaches=*/true);
		const FPlatformMemoryStats BaselineMemory = FPlatformMemory::GetStats();
		const int32 BaselineObjectCount =
			GUObjectArray.GetObjectArrayNumMinusAvailable();
		uint64 ObservedWorkloadPeak = BaselineMemory.UsedPhysical;
		const auto SampleWorkingSet = [&ObservedWorkloadPeak]()
		{
			ObservedWorkloadPeak = FMath::Max(
				ObservedWorkloadPeak,
				FPlatformMemory::GetStats().UsedPhysical);
		};
		TArray<double> CheckpointLatenciesMilliseconds;
		CheckpointLatenciesMilliseconds.Reserve(PeriodicCheckpointCount);
		TArray<uint64> EpochWorkingSets;
		EpochWorkingSets.Reserve(
			PeriodicCheckpointCount / GcIntervalCheckpoints);
		TArray<int32> SampleTicks;
		TArray<FGuid> SampleRoots;
		TArray<bool> SampleCapabilityStates;

		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		for (int32 TurnOrdinal = 0; TurnOrdinal < TotalTurns; ++TurnOrdinal)
		{
			const int32 Turn = FirstTurn + TurnOrdinal;
			const bool bGrant = (TurnOrdinal % 2) == 0;
			const int32 CompletedTurns = TurnOrdinal + 1;
			const bool bCheckpointBoundary =
				(CompletedTurns % CheckpointIntervalTurns) == 0;
			const int32 CheckpointOrdinal = bCheckpointBoundary
				? CompletedTurns / CheckpointIntervalTurns
				: 0;
			const bool bGcBoundary = bCheckpointBoundary
				&& (CheckpointOrdinal % GcIntervalCheckpoints) == 0;
			if (bGcBoundary)
			{
				Writer->HoldNextCheckpointEncodeForTests();
			}
			const double StartedAt = FPlatformTime::Seconds();
			ASSERT_THAT(IsTrue(AdvanceTurn(
				*Source, *Writer, Turn, TicksPerTurn, bGrant)));
			if (!Writer->IsRecording())
			{
				break;
			}
			SampleWorkingSet();
			if (!bCheckpointBoundary)
			{
				continue;
			}
			if (bGcBoundary)
			{
				bool bWorkerHeld = false;
				const bool bReachedHeldWorker = PumpGameThreadTasksUntil([&]()
				{
					SampleWorkingSet();
					bWorkerHeld = bWorkerHeld
						|| Writer->WaitForHeldCheckpointEncodeForTests(1);
					return bWorkerHeld || !Writer->IsRecording();
				}, 15.0);
				SampleWorkingSet();
				double GcStartedAt = 0.0;
				double GcFinishedAt = 0.0;
				double WorkerReleasedAt = 0.0;
				if (bWorkerHeld)
				{
					TFuture<double> DelayedRelease = Writer
						->ReleaseHeldCheckpointEncodeAfterDelayForTests(50);
					GcStartedAt = FPlatformTime::Seconds();
					CollectGarbage(RF_NoFlags);
					GcFinishedAt = FPlatformTime::Seconds();
					WorkerReleasedAt = DelayedRelease.Get();
				}
				else
				{
					Writer->ReleaseHeldCheckpointEncodeForTests();
				}
				SampleWorkingSet();
				ASSERT_THAT(IsTrue(bReachedHeldWorker));
				ASSERT_THAT(IsTrue(bWorkerHeld));
				ASSERT_THAT(IsTrue(WorkerReleasedAt > GcStartedAt));
				ASSERT_THAT(IsTrue(WorkerReleasedAt <= GcFinishedAt));
				ASSERT_THAT(IsTrue(Writer->IsRecording()));
			}

			const int32 ExpectedCheckpointCount = CheckpointOrdinal + 1;
			// This soak bounds each complete natural file cycle independently.
			// Dedicated replay overlap/slow-storage tests qualify continued turns
			// and resident pressure while a worker remains blocked.
			ASSERT_THAT(IsTrue(PumpGameThreadTasksUntil([&]()
			{
				SampleWorkingSet();
				return !Writer->IsRecording()
					|| (Writer->GetPersistedCheckpointCount()
						== ExpectedCheckpointCount
						&& !Writer->IsCheckpointEncodePending()
						&& !Writer->IsCheckpointAppendPending());
			}, 15.0)));
			ASSERT_THAT(IsTrue(Writer->IsRecording()));
			ASSERT_THAT(AreEqual(
				ExpectedCheckpointCount,
				Writer->GetPersistedCheckpointCount()));
			ASSERT_THAT(AreEqual(
				CompletedTurns, Writer->GetPersistedTurnCount()));
			ASSERT_THAT(AreEqual(0, Writer->GetResidentTurnCount()));
			ASSERT_THAT(AreEqual(
				static_cast<uint64>(0), Writer->GetResidentBytes()));
			ASSERT_THAT(AreEqual(
				static_cast<int64>(Writer->GetPersistedBytes()),
				IFileManager::Get().FileSize(*ReplayFile.Path)));
			ASSERT_THAT(AreEqual(
				StableCacheBytes,
				Source->GetComponentStorageSnapshotCacheBytesForTests()));

			CheckpointLatenciesMilliseconds.Add(
				(FPlatformTime::Seconds() - StartedAt) * 1000.0);
			if ((CheckpointOrdinal % SampleIntervalCheckpoints) == 0)
			{
				FGuid Root;
				ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(Root, Error)));
				SampleTicks.Add(Source->GetCurrentTick());
				SampleRoots.Add(Root);
				SampleCapabilityStates.Add(bGrant);
			}
			if (bGcBoundary)
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(
					ENamedThreads::GameThread);
				EpochWorkingSets.Add(
					FPlatformMemory::GetStats().UsedPhysical);
			}
		}

		ASSERT_THAT(IsTrue(Writer->IsRecording()));
		ASSERT_THAT(AreEqual(
			PeriodicCheckpointCount,
			CheckpointLatenciesMilliseconds.Num()));
		ASSERT_THAT(AreEqual(
			PeriodicCheckpointCount + 1,
			Writer->GetPersistedCheckpointCount()));
		ASSERT_THAT(AreEqual(PeriodicTurns, Writer->GetPersistedTurnCount()));
		ASSERT_THAT(AreEqual(1, Writer->GetResidentTurnCount()));
		ASSERT_THAT(AreEqual(
			InitialCacheHits + PeriodicCheckpointCount,
			Source->GetComponentStorageSnapshotCacheHitCountForTests()));
		ASSERT_THAT(IsTrue(
			Writer->GetPeakResidentTurnCount()
				<= CheckpointIntervalTurns));
		ASSERT_THAT(IsTrue(SampleCapabilityStates.Contains(true)));
		ASSERT_THAT(IsTrue(SampleCapabilityStates.Contains(false)));
		ASSERT_THAT(IsTrue(Source->HasPairCapability(
			FSeinPlayerID(1),
			FSeinPlayerID(2),
			SeinARTSTags::Relationship_Capability_ShareVision)));

		FGuid SourceFinalRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceFinalRoot, Error)));
		const int32 FinalTick = Source->GetCurrentTick();
		Source->StopSimulation();
		const FString PublishedPath = Writer->FinishRecording();
		ASSERT_THAT(IsFalse(PublishedPath.IsEmpty()));
		if (!PublishedPath.IsEmpty())
		{
			ReplayFile.Path = PublishedPath;
		}
		WorkerDrain.Writer = nullptr;

		CollectGarbage(RF_NoFlags);
		const FPlatformMemoryStats UntrimmedFinalMemory =
			FPlatformMemory::GetStats();
		FMemory::Trim(/*bTrimThreadCaches=*/true);
		FPlatformProcess::SleepNoStats(0.010f);
		const FPlatformMemoryStats FinalMemory = FPlatformMemory::GetStats();
		SampleWorkingSet();
		const int32 FinalObjectCount =
			GUObjectArray.GetObjectArrayNumMinusAvailable();
		const uint64 BaselineGrowth = PositiveDelta(
			FinalMemory.UsedPhysical, BaselineMemory.UsedPhysical);
		const uint64 CommitGrowth = PositiveDelta(
			FinalMemory.UsedVirtual, BaselineMemory.UsedVirtual);
		const uint64 PeakGrowth = PositiveDelta(
			ObservedWorkloadPeak,
			BaselineMemory.UsedPhysical);
		const uint64 LateGrowth = EpochWorkingSets.Num() >= 2
			? PositiveDelta(EpochWorkingSets.Last(), EpochWorkingSets[0])
			: 0;
		const double P50Latency = Percentile(
			CheckpointLatenciesMilliseconds, 0.50);
		const double P95Latency = Percentile(
			CheckpointLatenciesMilliseconds, 0.95);
		const double P99Latency = Percentile(
			CheckpointLatenciesMilliseconds, 0.99);
		const double MaximumLatency = Percentile(
			CheckpointLatenciesMilliseconds, 1.0);
		UE_LOG(LogTemp, Display,
			TEXT("Replay operational soak: %d turns, %d periodic checkpoints, %lld bytes, latency p50 %.3f ms / p95 %.3f ms / p99 %.3f ms / max %.3f ms, working set cold %.2f MiB / GC-control baseline %.2f MiB / untrimmed final %.2f MiB / trimmed final %.2f MiB / replay growth %.2f MiB / late growth %.2f MiB / observed workload peak %.2f MiB / peak growth %.2f MiB, private commit growth %.2f MiB, UObject count %d -> %d"),
			TotalTurns,
			PeriodicCheckpointCount,
			IFileManager::Get().FileSize(*ReplayFile.Path),
			P50Latency,
			P95Latency,
			P99Latency,
			MaximumLatency,
			static_cast<double>(ColdMemory.UsedPhysical) / MiB,
			static_cast<double>(BaselineMemory.UsedPhysical) / MiB,
			static_cast<double>(UntrimmedFinalMemory.UsedPhysical) / MiB,
			static_cast<double>(FinalMemory.UsedPhysical) / MiB,
			static_cast<double>(BaselineGrowth) / MiB,
			static_cast<double>(LateGrowth) / MiB,
			static_cast<double>(ObservedWorkloadPeak) / MiB,
			static_cast<double>(PeakGrowth) / MiB,
			static_cast<double>(CommitGrowth) / MiB,
			BaselineObjectCount,
			FinalObjectCount);
		ASSERT_THAT(IsTrue(P95Latency < MaximumP95LatencyMilliseconds));
		ASSERT_THAT(IsTrue(MaximumLatency < MaximumLatencyMilliseconds));
		ASSERT_THAT(IsTrue(
			BaselineGrowth < MaximumWorkingSetGrowthBytes));
		ASSERT_THAT(IsTrue(CommitGrowth < MaximumCommitGrowthBytes));
		ASSERT_THAT(IsTrue(PeakGrowth < MaximumPeakGrowthBytes));
		ASSERT_THAT(IsTrue(LateGrowth < MaximumLateGrowthBytes));
		ASSERT_THAT(IsTrue(FinalObjectCount <= BaselineObjectCount + 32));
		ASSERT_THAT(IsTrue(
			EpochWorkingSets.Num()
				== PeriodicCheckpointCount / GcIntervalCheckpoints));

		ASSERT_THAT(AreEqual(SampleTicks.Num(), SampleRoots.Num()));
		ASSERT_THAT(AreEqual(
			SampleTicks.Num(), SampleCapabilityStates.Num()));
		for (int32 SampleIndex = 0;
			SampleIndex < SampleTicks.Num();
			++SampleIndex)
		{
			FActorTestSpawner ProbeSpawner;
			USeinWorldSubsystem* Probe =
				ProbeSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			ASSERT_THAT(IsNotNull(Probe));
			TStrongObjectPtr<USeinReplayReader> Reader(
				NewObject<USeinReplayReader>(&ProbeSpawner.GetWorld()));
			ASSERT_THAT(IsTrue(Reader->LoadFromFile(ReplayFile.Path)));
			ASSERT_THAT(IsTrue(Reader->PlayFromTick(SampleTicks[SampleIndex])));
			if (Reader->IsPlaying())
			{
				Reader->Stop();
			}
			else
			{
				ASSERT_THAT(IsTrue(Probe->StartSimulation()));
			}
			ASSERT_THAT(AreEqual(
				SampleTicks[SampleIndex], Probe->GetCurrentTick()));
			ASSERT_THAT(AreEqual(
				SampleCapabilityStates[SampleIndex],
				Probe->HasPairCapability(
					FSeinPlayerID(1),
					FSeinPlayerID(2),
					SeinARTSTags::Relationship_Capability_ShareVision)));
			FGuid ProbeRoot;
			ASSERT_THAT(IsTrue(
				Probe->ComputeCanonicalStateRoot(ProbeRoot, Error)));
			ASSERT_THAT(AreEqual(
				SampleRoots[SampleIndex].ToString(EGuidFormats::Digits),
				ProbeRoot.ToString(EGuidFormats::Digits)));
			Probe->StopSimulation();
		}

		FActorTestSpawner FullTargetSpawner;
		USeinWorldSubsystem* FullTarget =
			FullTargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(FullTarget));
		TStrongObjectPtr<USeinReplayReader> FullReader(
			NewObject<USeinReplayReader>(&FullTargetSpawner.GetWorld()));
		ASSERT_THAT(IsTrue(FullReader->LoadFromFile(ReplayFile.Path)));
		ASSERT_THAT(IsTrue(FullReader->Play()));
		for (int32 Pump = 0;
			Pump < FinalTick * 4 && FullReader->IsPlaying();
			++Pump)
		{
			FTSTicker::GetCoreTicker().Tick(
				FullTarget->GetFixedDeltaTimeSeconds());
		}
		ASSERT_THAT(IsFalse(FullReader->IsPlaying()));
		ASSERT_THAT(AreEqual(FinalTick, FullTarget->GetCurrentTick()));
		ASSERT_THAT(IsTrue(FullTarget->StartSimulation()));
		FGuid FullTargetRoot;
		ASSERT_THAT(IsTrue(
			FullTarget->ComputeCanonicalStateRoot(FullTargetRoot, Error)));
		ASSERT_THAT(AreEqual(
			SourceFinalRoot.ToString(EGuidFormats::Digits),
			FullTargetRoot.ToString(EGuidFormats::Digits)));
		ASSERT_THAT(IsTrue(FullTarget->HasPairCapability(
			FSeinPlayerID(1),
			FSeinPlayerID(2),
			SeinARTSTags::Relationship_Capability_ShareVision)));
		FullTarget->StopSimulation();
	}

	TEST(ReplayCapacityExhaustionPreservesTheLastDurableFrontier,
		"SeinARTS.Integration.Network.Replay.Capacity")
	{
		using namespace ReplayOperationalSoakTestLocal;
		constexpr int32 MaximumFileSizeMiB = 64;
		constexpr int32 MaximumAttempts = 128;
		FScopedReplaySettings ReplaySettings(
			/*CheckpointIntervalTurns=*/1,
			/*TurnBatchSize=*/1,
			MaximumFileSizeMiB);
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FString Error;
		TStrongObjectPtr<USeinReplayWriter> Writer(StartRecording(
			*Source,
			MakeTwoPlayerMatchSettings(),
			/*Population=*/0,
			Error));
		if (!Writer.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("Replay capacity setup failed: %s"), *Error);
		}
		ASSERT_THAT(IsTrue(Writer.IsValid()));
		if (!Writer.IsValid())
		{
			return;
		}
		FScopedReplayFile PartialFile{Writer->GetActivePartialPath()};
		FScopedReplayWorkerDrain WorkerDrain{Writer.Get()};

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		ASSERT_THAT(IsNotNull(Settings));
		const int32 TicksPerTurn = FMath::Max(
			1, Settings->SimulationTickRate / Settings->TurnRate);
		const int32 FirstTurn = FMath::Max(1, Settings->InputDelayTurns);
		TMap<int32, FGuid> RootsByTick;
		TMap<int32, bool> CapabilityByTick;
		ASSERT_THAT(IsTrue(Source->StartSimulation()));
		TestRunner->AddExpectedError(
			TEXT("file-size policy is exhausted"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		for (int32 Attempt = 0;
			Attempt < MaximumAttempts && Writer->IsRecording();
			++Attempt)
		{
			const int32 Turn = FirstTurn + Attempt;
			const bool bGrant = (Attempt % 2) == 0;
			ASSERT_THAT(IsTrue(AdvanceTurn(
				*Source, *Writer, Turn, TicksPerTurn, bGrant)));
			FGuid Root;
			ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(Root, Error)));
			RootsByTick.Add(Source->GetCurrentTick(), Root);
			CapabilityByTick.Add(Source->GetCurrentTick(), bGrant);
			const int32 ExpectedCheckpointCount = Attempt + 2;
			ASSERT_THAT(IsTrue(PumpGameThreadTasksUntil([&]()
			{
				return !Writer->IsRecording()
					|| (Writer->GetPersistedCheckpointCount()
						== ExpectedCheckpointCount
						&& !Writer->IsCheckpointEncodePending()
						&& !Writer->IsCheckpointAppendPending());
			}, 15.0)));
		}
		Source->StopSimulation();

		ASSERT_THAT(IsFalse(Writer->IsRecording()));
		ASSERT_THAT(IsFalse(Writer->IsCheckpointEncodePending()));
		ASSERT_THAT(IsFalse(Writer->IsCheckpointAppendPending()));
		ASSERT_THAT(IsTrue(Writer->GetPersistedCheckpointCount() > 1));
		ASSERT_THAT(IsTrue(Writer->GetPersistedTurnCount() > 0));
		ASSERT_THAT(IsTrue(IFileManager::Get().FileExists(*PartialFile.Path)));
		const int64 PartialBytes =
			IFileManager::Get().FileSize(*PartialFile.Path);
		ASSERT_THAT(IsTrue(PartialBytes > 0));
		ASSERT_THAT(IsTrue(
			PartialBytes <= MaximumFileSizeMiB * static_cast<int64>(MiB)));
		WorkerDrain.Writer = nullptr;

		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target =
			TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));
		TStrongObjectPtr<USeinReplayReader> Reader(
			NewObject<USeinReplayReader>(&TargetSpawner.GetWorld()));
		ASSERT_THAT(IsTrue(Reader->LoadFromFile(PartialFile.Path)));
		const int32 DurableEndTick = Reader->GetHeader().EndTick;
		ASSERT_THAT(IsTrue(DurableEndTick > 0));
		ASSERT_THAT(IsTrue(RootsByTick.Contains(DurableEndTick)));
		ASSERT_THAT(IsTrue(CapabilityByTick.Contains(DurableEndTick)));
		ASSERT_THAT(IsTrue(Reader->Play()));
		for (int32 Pump = 0;
			Pump < DurableEndTick * 4 && Reader->IsPlaying();
			++Pump)
		{
			FTSTicker::GetCoreTicker().Tick(
				Target->GetFixedDeltaTimeSeconds());
		}
		ASSERT_THAT(IsFalse(Reader->IsPlaying()));
		ASSERT_THAT(AreEqual(DurableEndTick, Target->GetCurrentTick()));
		ASSERT_THAT(IsTrue(Target->StartSimulation()));
		ASSERT_THAT(AreEqual(
			CapabilityByTick.FindChecked(DurableEndTick),
			Target->HasPairCapability(
				FSeinPlayerID(1),
				FSeinPlayerID(2),
				SeinARTSTags::Relationship_Capability_ShareVision)));
		FGuid TargetRoot;
		ASSERT_THAT(IsTrue(
			Target->ComputeCanonicalStateRoot(TargetRoot, Error)));
		ASSERT_THAT(AreEqual(
			RootsByTick.FindChecked(DurableEndTick).ToString(EGuidFormats::Digits),
			TargetRoot.ToString(EGuidFormats::Digits)));
		Target->StopSimulation();
	}
}
