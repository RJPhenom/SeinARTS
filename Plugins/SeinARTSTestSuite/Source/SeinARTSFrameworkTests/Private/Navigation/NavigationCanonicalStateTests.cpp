#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Data/SeinWorldSnapshot.h"
#include "SeinNavigationSubsystem.h"
#include "SeinPathTypes.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace UE::SeinARTSTests
{
	struct FNavigationCanonicalStateTestAccess
	{
		static void Seed(USeinNavigationSubsystem& Navigation)
		{
			Navigation.PathRequestsThisTick = 3;
			Navigation.LastResetTick = 0;
			Navigation.LastDrainTick = 0;

			FSeinPathRequest Queued;
			Queued.Start = FFixedVector(
				FFixedPoint::FromInt(10),
				FFixedPoint::FromInt(20),
				FFixedPoint::Zero);
			Queued.End = FFixedVector(
				FFixedPoint::FromInt(30),
				FFixedPoint::FromInt(40),
				FFixedPoint::Zero);
			Queued.Requester = FSeinEntityHandle(2, 3);
			Queued.AgentNavLayerMask = 0x04;
			Queued.AgentWallPaddingCells = 2;
			Navigation.AsyncQueue.Add(Queued.Requester, Queued);

			FSeinPathRequest ReadyRequest;
			ReadyRequest.Start = FFixedVector(
				FFixedPoint::FromInt(50),
				FFixedPoint::FromInt(60),
				FFixedPoint::Zero);
			ReadyRequest.End = FFixedVector(
				FFixedPoint::FromInt(70),
				FFixedPoint::FromInt(80),
				FFixedPoint::Zero);
			ReadyRequest.Requester = FSeinEntityHandle(5, 2);
			ReadyRequest.bAuthoritativeDestination = true;

			USeinNavigationSubsystem::FSeinAsyncPathResult Ready;
			Ready.Request = ReadyRequest;
			Ready.Path.Waypoints = {
				ReadyRequest.Start,
				ReadyRequest.End,
			};
			Ready.Path.bIsValid = true;
			Ready.Path.DeriveSegmentsFromWaypoints();
			Navigation.AsyncResults.Add(
				ReadyRequest.Requester, MoveTemp(Ready));
		}

		static bool MatchesSeed(
			const USeinNavigationSubsystem& Navigation)
		{
			const FSeinPathRequest* Queued =
				Navigation.AsyncQueue.Find(FSeinEntityHandle(2, 3));
			const USeinNavigationSubsystem::FSeinAsyncPathResult* Ready =
				Navigation.AsyncResults.Find(FSeinEntityHandle(5, 2));
			return Navigation.PathRequestsThisTick == 3
				&& Navigation.LastResetTick == 0
				&& Navigation.LastDrainTick == 0
				&& Navigation.AsyncQueue.Num() == 1
				&& Navigation.AsyncResults.Num() == 1
				&& Queued
				&& Queued->End.X == FFixedPoint::FromInt(30)
				&& Queued->AgentNavLayerMask == 0x04
				&& Queued->AgentWallPaddingCells == 2
				&& Ready
				&& Ready->Request.bAuthoritativeDestination
				&& Ready->Path.bIsValid
				&& Ready->Path.Waypoints.Num() == 2
				&& Ready->Path.Waypoints.Last().Y
					== FFixedPoint::FromInt(80);
		}
	};

	namespace
	{
		bool StartNavigationStateWorld(
			USeinWorldSubsystem& World,
			FName FixtureId,
			FString& OutError)
		{
			return SeinTestMatchBootstrap::Materialize(
				World,
				FSeinMatchSettings(),
				0x4E415653,
				FixtureId,
				&OutError)
				&& SeinTestMatchBootstrap::Start(World, &OutError);
		}
	}

	TEST(NavigationContinuationChangesCanonicalWorldRoot,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World, TEXT("NavigationState.Root"), Error)));

		FGuid EmptyRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(EmptyRoot, Error)));
		FNavigationCanonicalStateTestAccess::Seed(*Navigation);

		FGuid SeededRoot;
		ASSERT_THAT(IsTrue(
			World->ComputeCanonicalStateRoot(SeededRoot, Error)));
		ASSERT_THAT(IsTrue(SeededRoot.IsValid()));
		ASSERT_THAT(IsTrue(SeededRoot != EmptyRoot));

		World->StopSimulation();
	}

	TEST(NavigationContinuationRoundTripsThroughSnapshot,
		"SeinARTS.Determinism.Navigation.CanonicalState")
	{
		FActorTestSpawner SourceSpawner;
		UWorld& SourceUnrealWorld = SourceSpawner.GetWorld();
		USeinWorldSubsystem* Source =
			SourceUnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* SourceNavigation =
			SourceUnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Source));
		ASSERT_THAT(IsNotNull(SourceNavigation));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*Source, TEXT("NavigationState.RoundTrip"), Error)));
		FNavigationCanonicalStateTestAccess::Seed(*SourceNavigation);

		FGuid SourceRoot;
		ASSERT_THAT(IsTrue(
			Source->ComputeCanonicalStateRoot(SourceRoot, Error)));
		FSeinWorldSnapshot Snapshot;
		Source->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));
		Source->StopSimulation();

		FActorTestSpawner DestinationSpawner;
		UWorld& DestinationUnrealWorld = DestinationSpawner.GetWorld();
		USeinWorldSubsystem* Destination =
			DestinationUnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* DestinationNavigation =
			DestinationUnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(Destination));
		ASSERT_THAT(IsNotNull(DestinationNavigation));
		ASSERT_THAT(IsTrue(
			SeinTestSnapshotRestore::RestoreTrusted(
				*Destination, Snapshot)));
		ASSERT_THAT(IsTrue(
			FNavigationCanonicalStateTestAccess::MatchesSeed(
				*DestinationNavigation)));

		FGuid DestinationRoot;
		ASSERT_THAT(IsTrue(
			Destination->ComputeCanonicalStateRoot(
				DestinationRoot, Error)));
		ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

		Destination->StopSimulation();
	}

	TEST(ModulePreUnloadReleaseSeversLiveNavigationSystems,
		"SeinARTS.Unit.Navigation.CanonicalState")
	{
		FActorTestSpawner Spawner;
		UWorld& UnrealWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
		USeinNavigationSubsystem* Navigation =
			UnrealWorld.GetSubsystem<USeinNavigationSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsNotNull(Navigation));
		ASSERT_THAT(IsNotNull(Navigation->GetNavigation()));

		FString Error;
		ASSERT_THAT(IsTrue(StartNavigationStateWorld(
			*World, TEXT("NavigationState.ModulePreUnload"), Error)));
		ASSERT_THAT(IsTrue(World->IsSimulationRunning()));

		TestRunner->AddExpectedError(
			TEXT("withdrew live state"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		Navigation->ReleaseModuleOwnedStateForModuleUnload();

		ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
		ASSERT_THAT(IsFalse(World->IsExecutionTopologyValid()));
		ASSERT_THAT(IsNull(Navigation->GetNavigation()));

		// ModuleManager may invoke ordinary world teardown after its pre-unload
		// pass. The second release must not touch the dead system pointer.
		Navigation->ReleaseModuleOwnedStateForModuleUnload();
	}
}
