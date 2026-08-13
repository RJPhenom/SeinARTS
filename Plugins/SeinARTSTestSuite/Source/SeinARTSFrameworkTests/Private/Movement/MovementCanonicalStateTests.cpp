#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinMovementComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "Movement/SeinMovement.h"
#include "SeinPathTypes.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"
#include "Testing/SeinMovementCanonicalStateTestAccess.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"

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

namespace UE::SeinARTSTests
{
	struct FSeinMovementLongRangeTestAccess
	{
		static void AdvanceWaypointAlongPath(
			int32& WaypointIndex,
			const FSeinPath& Path,
			const FFixedVector& AgentPosition,
			FFixedPoint CloseRadius)
		{
			USeinMovement::AdvanceWaypointAlongPath(
				WaypointIndex, Path, AgentPosition, CloseRadius);
		}

		static FFixedVector ResolveLookAheadPoint(
			const FFixedVector& AgentPosition,
			const FSeinPath& Path,
			int32 WaypointIndex,
			FFixedPoint LookAhead)
		{
			return USeinMovement::ResolveLookAheadPoint(
				AgentPosition, Path, WaypointIndex, LookAhead);
		}

		static bool LegacyOvershoot(
			const FFixedVector& AgentPosition,
			const FFixedVector& FinalWaypoint,
			FFixedPoint VicinityRadiusSq)
		{
			return USeinMovement::IsOvershootArrival(
				AgentPosition,
				FinalWaypoint,
				FFixedQuaternion::Identity,
				FFixedPoint::Zero,
				VicinityRadiusSq,
				FFixedPoint::One);
		}

		static bool ExactOvershoot(
			const FFixedVector& AgentPosition,
			const FFixedVector& FinalWaypoint,
			FFixedPoint VicinityRadius)
		{
			return USeinMovement::IsOvershootArrivalRadius(
				AgentPosition,
				FinalWaypoint,
				FFixedQuaternion::Identity,
				FFixedPoint::Zero,
				VicinityRadius,
				FFixedPoint::One);
		}
	};
}

TEST(MovementLegacySquaredRadiusContextRemainsCompatible,
	"SeinARTS.Unit.Movement.LongRange")
{
	FSeinEntity Entity;
	FSeinPath Path;
	int32 WaypointIndex = 0;
	FSeinMovementContext LegacyContext{
		Entity,
		nullptr,
		nullptr,
		Path,
		WaypointIndex,
		FFixedPoint::FromInt(2500),
		FFixedPoint::One,
		nullptr,
		nullptr,
		FSeinEntityHandle(),
	};
	ASSERT_THAT(IsTrue(
		SeinMath::Abs(
			LegacyContext.GetAcceptanceRadius()
				- FFixedPoint::FromInt(50))
			< FFixedPoint::KindaSmallNumber));
	LegacyContext.AcceptanceRadiusSq = FFixedPoint::FromInt(2);
	ASSERT_THAT(IsTrue(LegacyContext.IsWithinPlanarAcceptance(
		FFixedVector(
			FFixedPoint::One, FFixedPoint::One, FFixedPoint::Zero),
		FFixedVector::ZeroVector)));
	ASSERT_THAT(IsFalse(LegacyContext.IsWithinPlanarAcceptance(
		FFixedVector(
			FFixedPoint::FromInt(2), FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector::ZeroVector)));
	LegacyContext.AcceptanceRadiusSq = FFixedPoint::FromInt(2500);
	ASSERT_THAT(IsTrue(LegacyContext.IsWithinPlanarAcceptance(
		FFixedVector(
			FFixedPoint::FromInt(50), FFixedPoint(1), FFixedPoint::Zero),
		FFixedVector::ZeroVector)));
	LegacyContext.AcceptanceRadiusSq = FFixedPoint::MaxValue;
	ASSERT_THAT(IsFalse(LegacyContext.IsWithinPlanarAcceptance(
		FFixedVector(
			FFixedPoint::MinValue, FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(
			FFixedPoint::MaxValue, FFixedPoint::Zero, FFixedPoint::Zero))));

	const FFixedVector Agent(
		FFixedPoint::FromInt(100),
		FFixedPoint::Zero,
		FFixedPoint::Zero);
	ASSERT_THAT(IsTrue(
		UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
			LegacyOvershoot(
				Agent,
				FFixedVector::ZeroVector,
				FFixedPoint::FromInt(10000))));
	ASSERT_THAT(IsTrue(
		UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
			ExactOvershoot(
				Agent,
				FFixedVector::ZeroVector,
				FFixedPoint::FromInt(100))));
	ASSERT_THAT(IsTrue(
		UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
			LegacyOvershoot(
				FFixedVector(
					FFixedPoint::One,
					FFixedPoint::One,
					FFixedPoint::Zero),
				FFixedVector::ZeroVector,
				FFixedPoint::FromInt(2))));
}

TEST(MovementLongRangeWaypointAdvanceUsesExactDistance,
	"SeinARTS.Unit.Movement.LongRange")
{
	FSeinPath Path;
	Path.Waypoints = {
		FFixedVector(FFixedPoint::FromInt(50000),
			FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::FromInt(60000),
			FFixedPoint::Zero, FFixedPoint::Zero),
	};

	int32 WaypointIndex = 0;
	UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
		AdvanceWaypointAlongPath(
		WaypointIndex,
		Path,
		FFixedVector::ZeroVector,
		FFixedPoint::FromInt(50));
	ASSERT_THAT(AreEqual(0, WaypointIndex));
}

TEST(MovementLongRangeOffPathSegmentProjectionIsExact,
	"SeinARTS.Unit.Movement.LongRange")
{
	const FFixedVector Start(
		FFixedPoint::MinValue,
		FFixedPoint::Zero,
		FFixedPoint::Zero);
	const FFixedVector End(
		FFixedPoint::MaxValue,
		FFixedPoint::Zero,
		FFixedPoint::Zero);
	ASSERT_THAT(IsTrue(
		UE::SeinARTSTests::IsPointWithinMoveToSegmentForTest(
			FFixedVector::ZeroVector,
			Start,
			End,
			FFixedPoint::One)));
	ASSERT_THAT(IsFalse(
		UE::SeinARTSTests::IsPointWithinMoveToSegmentForTest(
			FFixedVector(
				FFixedPoint::Zero,
				FFixedPoint::FromInt(10),
				FFixedPoint::Zero),
			Start,
			End,
			FFixedPoint::One)));
}

TEST(MovementLongRangeWaypointCrossoverUsesDirection,
	"SeinARTS.Unit.Movement.LongRange")
{
	FSeinPath Path;
	Path.Waypoints = {
		FFixedVector::ZeroVector,
		FFixedVector(FFixedPoint::FromInt(50000),
			FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedVector(FFixedPoint::FromInt(60000),
			FFixedPoint::Zero, FFixedPoint::Zero),
	};

	int32 WaypointIndex = 1;
	UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
		AdvanceWaypointAlongPath(
		WaypointIndex,
		Path,
		FFixedVector(FFixedPoint::FromInt(40000),
			FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedPoint::FromInt(50));
	ASSERT_THAT(AreEqual(1, WaypointIndex));

	UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
		AdvanceWaypointAlongPath(
		WaypointIndex,
		Path,
		FFixedVector(FFixedPoint::FromInt(51000),
			FFixedPoint::Zero, FFixedPoint::Zero),
		FFixedPoint::FromInt(50));
	ASSERT_THAT(AreEqual(2, WaypointIndex));
}

TEST(MovementLongRangeLookAheadUsesExactDirection,
	"SeinARTS.Unit.Movement.LongRange")
{
	const FFixedVector Agent(
		FFixedPoint::MinValue + FFixedPoint::FromInt(100),
		FFixedPoint::Zero,
		FFixedPoint::Zero);
	FSeinPath Path;
	Path.Waypoints = {
		FFixedVector(
			FFixedPoint::MaxValue,
			FFixedPoint::Zero,
			FFixedPoint::Zero),
	};
	const FFixedVector LookAhead =
		UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
			ResolveLookAheadPoint(
				Agent, Path, 0, FFixedPoint::FromInt(50));
	ASSERT_THAT(IsTrue(LookAhead.X > Agent.X));
	ASSERT_THAT(IsTrue(FFixedVector::IsDistanceWithin(
		Agent, LookAhead, FFixedPoint::FromInt(50))));

	const FFixedVector SlopedAgent(
		Agent.X,
		FFixedPoint::Zero,
		FFixedPoint::MinValue);
	Path.Waypoints[0].Z = FFixedPoint::MaxValue;
	const FFixedVector SlopedLookAhead =
		UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
			ResolveLookAheadPoint(
				SlopedAgent, Path, 0, FFixedPoint::FromInt(50));
	ASSERT_THAT(IsTrue(SlopedLookAhead.Z > SlopedAgent.Z));
	ASSERT_THAT(IsTrue(
		SlopedLookAhead.Z
			< SlopedAgent.Z + FFixedPoint::FromInt(100)));

	FSeinPath EndpointPath;
	const FFixedVector Endpoint(
		FFixedPoint::FromInt(300),
		FFixedPoint::FromInt(300),
		FFixedPoint::MaxValue);
	EndpointPath.Waypoints = { Endpoint };
	const FFixedVector ExactEndpoint =
		UE::SeinARTSTests::FSeinMovementLongRangeTestAccess::
			ResolveLookAheadPoint(
				FFixedVector(
					FFixedPoint::Zero,
					FFixedPoint::Zero,
					FFixedPoint::MinValue),
				EndpointPath,
				0,
				FFixedPoint(1822200300701LL));
	ASSERT_THAT(IsTrue(ExactEndpoint == Endpoint));
}

TEST(MovementLongRangeArrivalSpeedCapSaturatesSafely,
	"SeinARTS.Unit.Movement.LongRange")
{
	const FFixedPoint Near = USeinMovement::KinematicArrivalSpeedCap(
		FFixedPoint::FromInt(100), FFixedPoint::FromInt(900));
	const FFixedPoint Far = USeinMovement::KinematicArrivalSpeedCap(
		FFixedPoint::FromInt(1000), FFixedPoint::FromInt(900));
	const FFixedPoint BeyondScalarSquare =
		USeinMovement::KinematicArrivalSpeedCap(
			FFixedPoint::FromInt(2000000),
			FFixedPoint::FromInt(2000));
	const FFixedPoint BillionScale =
		USeinMovement::KinematicArrivalSpeedCap(
			FFixedPoint::FromInt(1000000000),
			FFixedPoint::FromInt(1000000000));

	ASSERT_THAT(IsTrue(Near > FFixedPoint::Zero));
	ASSERT_THAT(AreEqual(int64(1822200299985LL), Near.Value));
	ASSERT_THAT(IsTrue(Far > Near));
	ASSERT_THAT(IsTrue(
		BeyondScalarSquare > FFixedPoint::FromInt(89000)));
	ASSERT_THAT(IsTrue(
		BeyondScalarSquare < FFixedPoint::FromInt(90000)));
	ASSERT_THAT(IsTrue(
		BillionScale > FFixedPoint::FromInt(1414213561)));
	ASSERT_THAT(IsTrue(
		BillionScale < FFixedPoint::FromInt(1414213563)));
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

TEST(MovementFootprintIncludesCompoundShapeOffset,
	"SeinARTS.Unit.Movement.NavigationPolicy")
{
	FSeinExtentsComponent Extents;
	FSeinExtentsShape Shape;
	Shape.Shape = ESeinExtentsShape::Capsule;
	Shape.Radius = FFixedPoint::FromInt(50);
	Shape.LocalOffset = FFixedVector(
		FFixedPoint::FromInt(300),
		FFixedPoint::FromInt(400),
		FFixedPoint::Zero);
	Extents.Shapes.Add(Shape);

	const FFixedPoint Resolved =
		USeinMovement::ResolveCollisionRadius(&Extents, nullptr);
	// Fixed-point sqrt is intentionally approximate; verify the geometry
	// contract without demanding an impossible integer-exact hypotenuse.
	ASSERT_THAT(IsTrue(Resolved > FFixedPoint::FromInt(549)));
	ASSERT_THAT(IsTrue(Resolved < FFixedPoint::FromInt(551)));
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
	const FGameplayTag BlockedTerrainTag =
		FGameplayTag::RequestGameplayTag(TEXT("Test"), false);
	ASSERT_THAT(IsTrue(BlockedTerrainTag.IsValid()));
	Source.Instance(0)->SetCachedNavigationPolicyForTest(
		BlockedTerrainTag,
		Source.Entities[0]);

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
	ASSERT_THAT(IsTrue(
		Restored->HasCachedNavigationPolicyForTest(
			BlockedTerrainTag,
			Source.Entities[0])));
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

TEST(MovementCoverageBatchWithdrawalIsAtomic,
	"SeinARTS.Unit.Movement.CanonicalState")
{
	FSeinMovementStateCoverageDescriptor Descriptor;
	Descriptor.NativeClass =
		USeinMoveToLifecycleTestMovement::StaticClass();
	Descriptor.Coverage =
		ESeinMovementStateCoverage::ReflectedComplete;

	TArray<FSeinMovementStateCoverageRegistrationHandle> Handles;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FString Error;
		FSeinMovementStateCoverageRegistrationHandle Handle =
			FSeinMovementStateCoverageRegistry::Register(
				TEXT("SeinARTSFrameworkTests"),
				Descriptor,
				&Error);
		ASSERT_THAT(IsTrue(Handle.IsValid()));
		Handles.Add(MoveTemp(Handle));
	}

	FString Error;
	ASSERT_THAT(IsTrue(
		FSeinMovementStateCoverageRegistry::UnregisterAll(
			Handles, &Error)));
	ASSERT_THAT(IsTrue(Handles.IsEmpty()));
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
