#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinMovementComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"
#include "Testing/SeinMovementCanonicalStateTestAccess.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool SealRoutineRoot(
		USeinWorldSubsystem& World,
		bool bForceFullRebuild,
		FGuid& OutRoot,
		FString& OutError)
	{
		return World.SealRoutineCanonicalStateRoot(
			World.GetCurrentTick(),
			bForceFullRebuild,
			OutRoot,
			OutError);
	}
};

namespace
{
	struct FMovementCanonicalFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		USeinMovementSubsystem* Movement = nullptr;
		TArray<FSeinEntityHandle> Entities;

		bool Initialize(
			TConstArrayView<int32> CreationOrder,
			FName FixtureId)
		{
			World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			Movement =
				Spawner.GetWorld().GetSubsystem<USeinMovementSubsystem>();
			if (!World || !Movement)
			{
				return false;
			}

			FString Error;
			if (!SeinTestMatchBootstrap::Materialize(
				*World,
				[&]()
				{
					for (int32 Index = 0;
						Index < CreationOrder.Num();
						++Index)
					{
						const FSeinEntityHandle Entity =
							World->SpawnAbstractEntity(
								FFixedTransform(),
								FSeinPlayerID::Neutral());
						FSeinMovementComponent Component;
						Component.MovementClass = FSoftClassPath(
							USeinMoveToLifecycleTestMovement::
								StaticClass()->GetPathName());
						World->AddComponent(Entity, Component);
						Entities.Add(Entity);
					}
				},
				FSeinMatchSettings(),
				0x4D4F5645,
				FixtureId,
				&Error)
				|| !SeinTestMatchBootstrap::Start(
					*World, &Error))
			{
				return false;
			}

			for (const int32 Index : CreationOrder)
			{
				if (!Entities.IsValidIndex(Index))
				{
					return false;
				}
				const FSeinMovementComponent* Component =
					World->GetComponent<FSeinMovementComponent>(
						Entities[Index]);
				if (!Component
					|| !Movement->GetOrCreateMovementInstance(
						Entities[Index], *Component))
				{
					return false;
				}
			}
			return true;
		}

		USeinMoveToLifecycleTestMovement* Instance(
			int32 Index) const
		{
			return Entities.IsValidIndex(Index)
				? Cast<USeinMoveToLifecycleTestMovement>(
					Movement->FindMovementInstance(Entities[Index]))
				: nullptr;
		}
	};

	FSeinCanonicalStateContributorRecord* FindMovementRecord(
		FSeinWorldSnapshot& Snapshot)
	{
		return Snapshot.NativeCanonicalStateRecords.
			FindByPredicate(
				[](const FSeinCanonicalStateContributorRecord& Record)
				{
					return Record.Key.StableDomainId
							== FName(TEXT("seinarts.movement"))
						&& Record.Key.StableContributorId
							== FName(TEXT("persistent-policy-instances"));
				});
	}
}

TEST(MovementReflectedStateChangesCanonicalRoot,
	"SeinARTS.Unit.Movement.CanonicalState")
{
	FMovementCanonicalFixture Fixture;
	const int32 Order[] = { 0 };
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		Order, TEXT("MovementState.Root"))));
	USeinMoveToLifecycleTestMovement* Instance =
		Fixture.Instance(0);
	ASSERT_THAT(IsNotNull(Instance));

	FString Error;
	FGuid Before;
	ASSERT_THAT(IsTrue(
		Fixture.World->ComputeCanonicalStateRoot(Before, Error)));
	Instance->PersistentTestValue = FFixedPoint::FromInt(37);
	FGuid After;
	ASSERT_THAT(IsTrue(
		Fixture.World->ComputeCanonicalStateRoot(After, Error)));
	ASSERT_THAT(IsTrue(Before != After));
	Fixture.World->StopSimulation();
}

TEST(MovementRoutineRootTracksDirtyInstanceAndMatchesForcedRebuild,
	"SeinARTS.Unit.Movement.CanonicalState")
{
	FMovementCanonicalFixture Fixture;
	const int32 Order[] = { 0 };
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		Order, TEXT("MovementState.RoutineRoot"))));
	USeinMoveToLifecycleTestMovement* Instance = Fixture.Instance(0);
	ASSERT_THAT(IsNotNull(Instance));

	FString Error;
	FGuid Before;
	ASSERT_THAT(IsTrue(
		FSeinWorldSubsystemTestAccess::SealRoutineRoot(
			*Fixture.World, true, Before, Error)));

	Instance->PersistentTestValue = FFixedPoint::FromInt(37);
	Fixture.Movement->MarkMovementStateDirty(Fixture.Entities[0]);
	FGuid Incremental;
	ASSERT_THAT(IsTrue(
		FSeinWorldSubsystemTestAccess::SealRoutineRoot(
			*Fixture.World, false, Incremental, Error)));
	ASSERT_THAT(IsTrue(Incremental != Before));

	FGuid Forced;
	ASSERT_THAT(IsTrue(
		FSeinWorldSubsystemTestAccess::SealRoutineRoot(
			*Fixture.World, true, Forced, Error)));
	ASSERT_THAT(IsTrue(Forced == Incremental));
	Fixture.World->StopSimulation();
}

TEST(MovementInstanceInsertionOrderIsCanonical,
	"SeinARTS.Determinism.Movement.CanonicalState")
{
	FMovementCanonicalFixture Forward;
	FMovementCanonicalFixture Reverse;
	const int32 ForwardOrder[] = { 0, 1 };
	const int32 ReverseOrder[] = { 1, 0 };
	ASSERT_THAT(IsTrue(Forward.Initialize(
		ForwardOrder, TEXT("MovementState.Order"))));
	ASSERT_THAT(IsTrue(Reverse.Initialize(
		ReverseOrder, TEXT("MovementState.Order"))));

	ASSERT_THAT(IsNotNull(Forward.Instance(0)));
	ASSERT_THAT(IsNotNull(Forward.Instance(1)));
	ASSERT_THAT(IsNotNull(Reverse.Instance(0)));
	ASSERT_THAT(IsNotNull(Reverse.Instance(1)));
	Forward.Instance(0)->PersistentTestValue =
		FFixedPoint::FromInt(11);
	Forward.Instance(1)->PersistentTestValue =
		FFixedPoint::FromInt(22);
	Reverse.Instance(0)->PersistentTestValue =
		FFixedPoint::FromInt(11);
	Reverse.Instance(1)->PersistentTestValue =
		FFixedPoint::FromInt(22);

	FString Error;
	FGuid ForwardRoot;
	FGuid ReverseRoot;
	ASSERT_THAT(IsTrue(
		Forward.World->ComputeCanonicalStateRoot(
			ForwardRoot, Error)));
	ASSERT_THAT(IsTrue(
		Reverse.World->ComputeCanonicalStateRoot(
			ReverseRoot, Error)));
	ASSERT_THAT(IsTrue(ForwardRoot == ReverseRoot));
	Forward.World->StopSimulation();
	Reverse.World->StopSimulation();
}

TEST(MovementReflectedStateRoundTripsTransactionally,
	"SeinARTS.Determinism.Movement.CanonicalState")
{
	FMovementCanonicalFixture Source;
	const int32 Order[] = { 0 };
	ASSERT_THAT(IsTrue(Source.Initialize(
		Order, TEXT("MovementState.RoundTrip"))));
	ASSERT_THAT(IsNotNull(Source.Instance(0)));
	Source.Instance(0)->PersistentTestValue =
		FFixedPoint::FromInt(73);

	FString Error;
	FGuid SourceRoot;
	ASSERT_THAT(IsTrue(
		Source.World->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
	FSeinWorldSnapshot Snapshot;
	Source.World->CaptureSnapshot(Snapshot);
	ASSERT_THAT(AreEqual(
		FSeinWorldSnapshot::CurrentVersion,
		Snapshot.SnapshotVersion));
	Source.World->StopSimulation();

	FActorTestSpawner DestinationSpawner;
	UWorld& DestinationUnrealWorld =
		DestinationSpawner.GetWorld();
	USeinWorldSubsystem* Destination =
		DestinationUnrealWorld.
			GetSubsystem<USeinWorldSubsystem>();
	USeinMovementSubsystem* DestinationMovement =
		DestinationUnrealWorld.
			GetSubsystem<USeinMovementSubsystem>();
	ASSERT_THAT(IsNotNull(Destination));
	ASSERT_THAT(IsNotNull(DestinationMovement));
	ASSERT_THAT(IsTrue(
		SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Snapshot)));

	USeinMoveToLifecycleTestMovement* Restored =
		Cast<USeinMoveToLifecycleTestMovement>(
			DestinationMovement->FindMovementInstance(
				Source.Entities[0]));
	ASSERT_THAT(IsNotNull(Restored));
	ASSERT_THAT(IsTrue(
		Restored->PersistentTestValue
			== FFixedPoint::FromInt(73)));
	FGuid DestinationRoot;
	ASSERT_THAT(IsTrue(
		Destination->ComputeCanonicalStateRoot(
			DestinationRoot, Error)));
	ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

	FSeinWorldSnapshot Corrupted = Snapshot;
	FSeinCanonicalStateContributorRecord* Record =
		FindMovementRecord(Corrupted);
	ASSERT_THAT(IsNotNull(Record));
	ASSERT_THAT(IsFalse(Record->PayloadBytes.IsEmpty()));
	Record->PayloadBytes.Last() ^= 0x01;
	TestRunner->AddExpectedError(
		TEXT("Contributor record leaf digest is invalid"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	ASSERT_THAT(IsFalse(
		SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Corrupted)));
	Restored = Cast<USeinMoveToLifecycleTestMovement>(
		DestinationMovement->FindMovementInstance(
			Source.Entities[0]));
	ASSERT_THAT(IsNotNull(Restored));
	ASSERT_THAT(IsTrue(
		Restored->PersistentTestValue
			== FFixedPoint::FromInt(73)));

	FSeinWorldSnapshot MissingClass = Snapshot;
	Record = FindMovementRecord(MissingClass);
	ASSERT_THAT(IsNotNull(Record));
	ASSERT_THAT(IsTrue(
		SeinReplaceFirstMovementClassPathForTest(
			*Record,
			TEXT("/Script/SeinARTSMovement.DefinitelyMissingMovement"),
			Error)));
	TestRunner->AddExpectedError(
		TEXT("Exact movement policy class"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	ASSERT_THAT(IsFalse(
		SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, MissingClass)));
	Restored = Cast<USeinMoveToLifecycleTestMovement>(
		DestinationMovement->FindMovementInstance(
			Source.Entities[0]));
	ASSERT_THAT(IsNotNull(Restored));
	ASSERT_THAT(IsTrue(
		Restored->PersistentTestValue
			== FFixedPoint::FromInt(73)));
	Destination->StopSimulation();
}

TEST(MovementCoverageDuplicateGenerationIsReloadSafe,
	"SeinARTS.Unit.Movement.CanonicalState")
{
	FMovementCanonicalFixture Fixture;
	const int32 Order[] = { 0 };
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		Order, TEXT("MovementState.Reload"))));

	FSeinMovementStateCoverageDescriptor Descriptor;
	Descriptor.NativeClass =
		USeinMoveToLifecycleTestMovement::StaticClass();
	Descriptor.Coverage =
		ESeinMovementStateCoverage::ReflectedComplete;
	FString Error;
	FSeinMovementStateCoverageRegistrationHandle Duplicate =
		FSeinMovementStateCoverageRegistry::Register(
			TEXT("SeinARTSFrameworkTests"),
			Descriptor,
			&Error);
	ASSERT_THAT(IsTrue(Duplicate.IsValid()));

	FGuid Root;
	ASSERT_THAT(IsTrue(
		Fixture.World->ComputeCanonicalStateRoot(Root, Error)));
	ASSERT_THAT(IsTrue(Root.IsValid()));
	Duplicate.Reset();
	ASSERT_THAT(IsTrue(
		Fixture.World->ComputeCanonicalStateRoot(Root, Error)));
	Fixture.World->StopSimulation();
}

TEST(MovementExtensionUnloadSeversNativeInstances,
	"SeinARTS.Unit.Movement.CanonicalState")
{
	FMovementCanonicalFixture Fixture;
	const int32 Order[] = { 0 };
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		Order, TEXT("MovementState.ModuleUnload"))));
	ASSERT_THAT(AreEqual(
		1, Fixture.Movement->GetMovementInstanceCount()));
	ASSERT_THAT(IsTrue(Fixture.World->IsSimulationRunning()));

	TestRunner->AddExpectedError(
		TEXT("Execution topology invalid:"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	Fixture.Movement->ReleaseNativeClassStateForModuleUnload(
		TEXT("SeinARTSFrameworkTests"));
	ASSERT_THAT(AreEqual(
		0, Fixture.Movement->GetMovementInstanceCount()));
	ASSERT_THAT(IsFalse(Fixture.World->IsSimulationRunning()));
	ASSERT_THAT(IsFalse(
		Fixture.World->IsExecutionTopologyValid()));
}
