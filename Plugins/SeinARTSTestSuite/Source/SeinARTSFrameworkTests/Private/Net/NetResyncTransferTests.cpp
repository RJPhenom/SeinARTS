#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Containers/Ticker.h"

#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinSnapshotTransfer.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinSnapshotRestoreAuthority.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		bool StartResyncSourceWorld(
			USeinWorldSubsystem& World,
			FName FixtureId,
			FString& OutError)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				FSeinMatchSettings(),
				0x52535943,
				FixtureId,
				&OutError)
				&& SeinTestMatchBootstrap::Start(World, &OutError);
		}

		void TickWorldOnce(USeinWorldSubsystem& World)
		{
			FTSTicker::GetCoreTicker().Tick(
				World.GetFixedDeltaTimeSeconds());
		}
	}

	TEST(CheckpointTransferEnvelopeRoundTripsAndRejectsTampering,
		"SeinARTS.Unit.Net.Resync")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		ASSERT_THAT(IsTrue(StartResyncSourceWorld(
			*World, TEXT("Resync.EnvelopeRoundTrip"), Error)));
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			TickWorldOnce(*World);
		}
		World->StopSimulation();

		FSeinWorldSnapshot Captured;
		FSeinWorldSnapshotReferenceGuard CapturedGCGuard(Captured);
		World->CaptureSnapshot(Captured);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Captured.SnapshotVersion));

		TArray<uint8> EnvelopeBytes;
		FSeinSnapshotEnvelopeMetadata Metadata;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::EncodeCheckpointEnvelope(
			Captured, EnvelopeBytes, Metadata, Error)));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(Captured.CurrentTick),
			Metadata.SnapshotTick));

		FSeinWorldSnapshot Decoded;
		FSeinWorldSnapshotReferenceGuard DecodedGCGuard(Decoded);
		FSeinSnapshotEnvelopeMetadata DecodedMetadata;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
			EnvelopeBytes, Decoded, DecodedMetadata, Error)));
		ASSERT_THAT(AreEqual(Captured.CurrentTick, Decoded.CurrentTick));
		ASSERT_THAT(IsTrue(
			Captured.CommandProtocolDigest
				== Decoded.CommandProtocolDigest));
		ASSERT_THAT(IsTrue(
			Captured.SimulationContentDigest
				== Decoded.SimulationContentDigest));
		ASSERT_THAT(AreEqual(Captured.SessionSeed, Decoded.SessionSeed));

		// One flipped payload byte must fail the exact-body digest closed.
		TArray<uint8> Tampered = EnvelopeBytes;
		Tampered[Tampered.Num() / 2] ^= 0x5A;
		FSeinWorldSnapshot TamperedOut;
		FSeinWorldSnapshotReferenceGuard TamperedGCGuard(TamperedOut);
		FSeinSnapshotEnvelopeMetadata TamperedMetadata;
		ASSERT_THAT(IsFalse(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
			Tampered, TamperedOut, TamperedMetadata, Error)));
		ASSERT_THAT(IsFalse(Error.IsEmpty()));

		// Truncation must fail closed too.
		TArray<uint8> Truncated = EnvelopeBytes;
		Truncated.SetNum(Truncated.Num() - 1, EAllowShrinking::Yes);
		ASSERT_THAT(IsFalse(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
			Truncated, TamperedOut, TamperedMetadata, Error)));
	}

	TEST(TransferredCheckpointAdoptsStoppedAndCatchesUpToIdenticalRoot,
		"SeinARTS.Unit.Net.Resync")
	{
		// Source world: bootstrap, advance, checkpoint mid-match.
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		FString Error;
		ASSERT_THAT(IsTrue(StartResyncSourceWorld(
			*Source, TEXT("Resync.CatchUp"), Error)));
		for (int32 Tick = 0; Tick < 4; ++Tick)
		{
			TickWorldOnce(*Source);
		}

		// Live-boundary capture: the coordinator checkpoints between fixed
		// ticks WITHOUT stopping its sim — the exact resync-serve posture.
		FSeinWorldSnapshot Checkpoint;
		FSeinWorldSnapshotReferenceGuard CheckpointGCGuard(Checkpoint);
		Source->CaptureSnapshot(Checkpoint);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Checkpoint.SnapshotVersion));
		const int32 FrontierTick = Checkpoint.CurrentTick;

		// The source keeps simulating past the checkpoint — this is the live
		// session the resyncing peer must catch up to.
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			TickWorldOnce(*Source);
		}
		const int32 LiveTick = Source->GetCurrentTick();
		ASSERT_THAT(IsTrue(LiveTick > FrontierTick));

		// Transfer through the real bounded envelope bytes.
		TArray<uint8> EnvelopeBytes;
		FSeinSnapshotEnvelopeMetadata Metadata;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::EncodeCheckpointEnvelope(
			Checkpoint, EnvelopeBytes, Metadata, Error)));
		FSeinWorldSnapshot Transferred;
		FSeinWorldSnapshotReferenceGuard TransferredGCGuard(Transferred);
		FSeinSnapshotEnvelopeMetadata TransferredMetadata;
		ASSERT_THAT(IsTrue(SeinSnapshotTransfer::DecodeCheckpointEnvelope(
			EnvelopeBytes, Transferred, TransferredMetadata, Error)));

		// Target world: adopt stopped with local state preserved (the resync
		// posture), under the open catch-up window.
		FActorTestSpawner TargetSpawner;
		USeinWorldSubsystem* Target =
			TargetSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Target));
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*Target,
			Transferred,
			FSeinSnapshotRestoreOptions(
				ESeinSnapshotLocalStateRestorePolicy::PreserveCurrent,
				ESeinSnapshotResumePolicy::RemainStopped))));
		ASSERT_THAT(AreEqual(FrontierTick, Target->GetCurrentTick()));
		ASSERT_THAT(IsFalse(Target->IsSimulationRunning()));
		ASSERT_THAT(IsTrue(Target->BeginResyncCatchUpWindow(Error)));

		// A catching-up peer must not emit checkpoints.
		TestRunner->AddExpectedError(
			TEXT("cannot produce checkpoints until resync activation completes"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FSeinWorldSnapshot Gated;
		FSeinWorldSnapshotReferenceGuard GatedGCGuard(Gated);
		Target->CaptureSnapshot(Gated);
		ASSERT_THAT(AreEqual(0, Gated.SnapshotVersion));

		// Both worlds share the ONE global core ticker, so only one may run
		// at a time or their tick counts couple. Advance the SOURCE alone to
		// the common comparison boundary (the adopted target is dormant —
		// RemainStopped keeps its reservation but its pump early-outs),
		// compute its root there, then freeze it.
		const int32 MaxTicksPerFrame =
			GetDefault<USeinARTSCoreSettings>()->MaxTicksPerFrame;
		const int32 CommonTick =
			FMath::Max(LiveTick, FrontierTick + MaxTicksPerFrame) + 2;
		while (Source->GetCurrentTick() < CommonTick)
		{
			TickWorldOnce(*Source);
		}
		ASSERT_THAT(AreEqual(CommonTick, Source->GetCurrentTick()));
		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		Source->StopSimulation();

		// The catch-up BURST: with the window open, one scheduler pump must
		// advance MULTIPLE ticks (real-time accumulation could never close a
		// wall-clock deficit — the deficit would stay constant forever). No
		// turn gate is bound in this standalone world, so the burst runs to
		// the per-frame cap; in production the lockstep gate bounds it to the
		// turns actually available.
		ASSERT_THAT(IsTrue(Target->StartSimulation()));
		const int32 TickBeforeBurst = Target->GetCurrentTick();
		TickWorldOnce(*Target);
		ASSERT_THAT(IsTrue(
			Target->GetCurrentTick() - TickBeforeBurst > 1));

		// Activation closes the window (dropping residual burst budget);
		// normal 1:1 pacing lands the target exactly on the boundary.
		Target->EndResyncCatchUpWindow();
		while (Target->GetCurrentTick() < CommonTick)
		{
			TickWorldOnce(*Target);
		}
		ASSERT_THAT(AreEqual(CommonTick, Target->GetCurrentTick()));

		// The activation handshake's exact criterion: identical canonical
		// roots at the same boundary.
		FGuid CaughtUpRoot;
		ASSERT_THAT(IsTrue(
			Target->ComputeCanonicalStateRoot(CaughtUpRoot, Error)));
		ASSERT_THAT(IsTrue(CaughtUpRoot == SourceRoot));
		Target->StopSimulation();
	}
}
