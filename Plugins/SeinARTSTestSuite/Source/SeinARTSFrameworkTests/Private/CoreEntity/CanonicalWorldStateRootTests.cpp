#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Data/SeinWorldSnapshot.h"
#include "Input/SeinCommand.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	namespace
	{
		bool StartRootTestWorld(
			USeinWorldSubsystem& World,
			FName FixtureId,
			FString& OutError)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				FSeinMatchSettings(),
				0x52544F4F,
				FixtureId,
				&OutError)
				&& SeinTestMatchBootstrap::Start(World, &OutError);
		}
	}

	TEST(CanonicalWorldStateRootIsStableAtAnUnchangedBoundary,
		"SeinARTS.Unit.CoreEntity.CanonicalState.WorldRoot")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		ASSERT_THAT(IsTrue(StartRootTestWorld(
			*World, TEXT("CanonicalRoot.Stable"), Error)));

		FGuid FirstRoot;
		FGuid SecondRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(FirstRoot, Error)));
		ASSERT_THAT(IsTrue(FirstRoot.IsValid()));
		ASSERT_THAT(IsTrue(Error.IsEmpty()));
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(SecondRoot, Error)));
		ASSERT_THAT(IsTrue(SecondRoot.IsValid()));
		ASSERT_THAT(IsTrue(Error.IsEmpty()));
		ASSERT_THAT(IsTrue(FirstRoot == SecondRoot));

		World->StopSimulation();
	}

	TEST(CanonicalWorldStateRootFailureLeavesOutputUnchanged,
		"SeinARTS.Unit.CoreEntity.CanonicalState.WorldRoot")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		ASSERT_THAT(IsTrue(StartRootTestWorld(
			*World, TEXT("CanonicalRoot.TransactionalFailure"), Error)));

		const FGuid Sentinel(
			0x10203040,
			0x50607080,
			0x90A0B0C0,
			0xD0E0F001);
		FGuid ObservedRoot = Sentinel;
		FString ObservedError;
		bool bCallbackRan = false;
		bool bCaptureSucceeded = true;
		const FDelegateHandle Handle =
			World->OnCaptureSnapshotPostSim.AddLambda(
				[World, &ObservedRoot, &ObservedError,
					&bCallbackRan, &bCaptureSucceeded](
					FSeinCameraSnapshotData&)
				{
					bCallbackRan = true;
					bCaptureSucceeded =
						World->ComputeCanonicalStateRoot(
							ObservedRoot, ObservedError);
				});

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		World->OnCaptureSnapshotPostSim.Remove(Handle);

		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		ASSERT_THAT(IsTrue(bCallbackRan));
		ASSERT_THAT(IsFalse(bCaptureSucceeded));
		ASSERT_THAT(IsTrue(ObservedRoot == Sentinel));
		ASSERT_THAT(IsTrue(ObservedError.Contains(
			TEXT("refused an in-flight simulation"))));

		World->StopSimulation();
	}

	TEST(CanonicalWorldStateRootIncludesPendingCommandContinuation,
		"SeinARTS.Unit.CoreEntity.CanonicalState.WorldRoot")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		ASSERT_THAT(IsTrue(StartRootTestWorld(
			*World, TEXT("CanonicalRoot.PendingCommand"), Error)));

		FGuid EmptyQueueRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(EmptyQueueRoot, Error)));
		ASSERT_THAT(AreEqual(0, World->GetPendingCommands().Num()));

		World->SubmitLocalCommandDraft(FSeinCommand::MakePingCommand(
			FSeinPlayerID(1), FFixedVector()));
		ASSERT_THAT(AreEqual(1, World->GetPendingCommands().Num()));

		FGuid PendingCommandRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(PendingCommandRoot, Error)));
		ASSERT_THAT(IsTrue(PendingCommandRoot.IsValid()));
		ASSERT_THAT(IsTrue(EmptyQueueRoot != PendingCommandRoot));

		World->StopSimulation();
	}

	TEST(CanonicalWorldStateRootRefusesReplayIngressTransactionally,
		"SeinARTS.Unit.CoreEntity.CanonicalState.WorldRoot")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FString Error;
		ASSERT_THAT(IsTrue(StartRootTestWorld(
			*World, TEXT("CanonicalRoot.ReplayIngress"), Error)));
		ASSERT_THAT(IsTrue(
			World->BeginReplayExclusiveCommandIngressForTests(Error)));

		const FGuid Sentinel(
			0x10203040,
			0x50607080,
			0x90A0B0C0,
			0xD0E0F001);
		FGuid ObservedRoot = Sentinel;
		ASSERT_THAT(IsFalse(
			World->ComputeCanonicalStateRoot(ObservedRoot, Error)));
		ASSERT_THAT(IsTrue(ObservedRoot == Sentinel));
		ASSERT_THAT(IsTrue(Error.Contains(
			TEXT("replay command ingress"))));

		World->EndReplayExclusiveCommandIngressForTests();
		World->StopSimulation();
	}

	TEST(SnapshotRestorePreservesCanonicalWorldStateRoot,
		"SeinARTS.Determinism.CanonicalState.WorldRoot")
	{
		FActorTestSpawner SourceSpawner;
		USeinWorldSubsystem* Source =
			SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Source));

		FString Error;
		ASSERT_THAT(IsTrue(StartRootTestWorld(
			*Source, TEXT("CanonicalRoot.SnapshotRoundTrip"), Error)));

		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));

		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		Source->StopSimulation();

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* Destination =
			DestinationSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsTrue(Destination->RestoreSnapshot(Snapshot)));
		ASSERT_THAT(IsTrue(Destination->IsSimulationRunning()));

		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

		Destination->StopSimulation();
	}
}
