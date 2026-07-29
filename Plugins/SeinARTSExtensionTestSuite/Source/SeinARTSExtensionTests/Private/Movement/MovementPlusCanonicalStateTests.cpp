#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinMovementComponent.h"
#include "Data/SeinFlyingMovementData.h"
#include "Data/SeinTrackedMovementData.h"
#include "Data/SeinWheeledMovementData.h"
#include "Data/SeinWorldSnapshot.h"
#include "Movement/SeinBasicMovement.h"
#include "Movement/SeinFlightMovement.h"
#include "Movement/SeinTrackedVehicleMovement.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FMovementPlusCanonicalFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		USeinMovementSubsystem* Movement = nullptr;
		TArray<FSeinEntityHandle> Entities;

		bool Initialize(FName FixtureId)
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
						auto Add = [this](
							const UClass* MovementClass,
							FInstancedStruct ClassData)
						{
							const FSeinEntityHandle Entity =
								World->SpawnAbstractEntity(
									FFixedTransform(),
									FSeinPlayerID::Neutral());
							FSeinMovementComponent Component;
							Component.MovementClass =
								FSoftClassPath(
									MovementClass->GetPathName());
							Component.MovementClassData =
								MoveTemp(ClassData);
							World->AddComponent(Entity, Component);
							Entities.Add(Entity);
						};

						Add(
							USeinWheeledVehicleMovement::StaticClass(),
							FInstancedStruct::Make(
								FSeinWheeledMovementData()));
						Add(
							USeinTrackedVehicleMovement::StaticClass(),
							FInstancedStruct::Make(
								FSeinTrackedMovementData()));
						Add(
							USeinFlightMovement::StaticClass(),
							FInstancedStruct::Make(
								FSeinFlyingMovementData()));
					},
					FSeinMatchSettings(),
					0x4D504C55,
					FixtureId,
					&Error)
				|| !SeinTestMatchBootstrap::Start(
					*World, &Error))
			{
				return false;
			}

			for (const FSeinEntityHandle Entity : Entities)
			{
				const FSeinMovementComponent* Component =
					World->GetComponent<FSeinMovementComponent>(
						Entity);
				if (!Component
					|| !Movement->GetOrCreateMovementInstance(
						Entity, *Component))
				{
					return false;
				}
			}
			return true;
		}

		UObject* Instance(int32 Index) const
		{
			return Entities.IsValidIndex(Index)
				? Movement->FindMovementInstance(Entities[Index])
				: nullptr;
		}
	};

	bool SetFixedState(
		UObject& Object,
		FName PropertyName,
		FFixedPoint Value)
	{
		FStructProperty* Property =
			FindFProperty<FStructProperty>(
				Object.GetClass(), PropertyName);
		if (!Property
			|| Property->Struct != FFixedPoint::StaticStruct())
		{
			return false;
		}
		*Property->ContainerPtrToValuePtr<FFixedPoint>(&Object) =
			Value;
		return true;
	}

	bool GetFixedState(
		const UObject& Object,
		FName PropertyName,
		FFixedPoint& OutValue)
	{
		const FStructProperty* Property =
			FindFProperty<FStructProperty>(
				Object.GetClass(), PropertyName);
		if (!Property
			|| Property->Struct != FFixedPoint::StaticStruct())
		{
			return false;
		}
		OutValue =
			*Property->ContainerPtrToValuePtr<FFixedPoint>(
				&Object);
		return true;
	}
}

TEST(MovementPlusReflectedStateRoundTripsAndContinues,
	"SeinARTS.Determinism.MovementPlus.CanonicalState")
{
	FMovementPlusCanonicalFixture Source;
	ASSERT_THAT(IsTrue(Source.Initialize(
		TEXT("MovementPlusState.RoundTrip"))));
	ASSERT_THAT(AreEqual(3, Source.Entities.Num()));

	UObject* Wheeled = Source.Instance(0);
	UObject* Tracked = Source.Instance(1);
	UObject* Flight = Source.Instance(2);
	ASSERT_THAT(IsNotNull(Wheeled));
	ASSERT_THAT(IsNotNull(Tracked));
	ASSERT_THAT(IsNotNull(Flight));

	FString Error;
	FGuid Before;
	ASSERT_THAT(IsTrue(
		Source.World->ComputeCanonicalStateRoot(
			Before, Error)));
	ASSERT_THAT(IsTrue(SetFixedState(
		*Wheeled, TEXT("CurrentSteer"),
		FFixedPoint::FromInt(11))));
	ASSERT_THAT(IsTrue(SetFixedState(
		*Tracked, TEXT("CurrentYawRate"),
		FFixedPoint::FromInt(-7))));
	ASSERT_THAT(IsTrue(SetFixedState(
		*Flight, TEXT("CurrentSteer"),
		FFixedPoint::FromInt(5))));

	FGuid SourceRoot;
	ASSERT_THAT(IsTrue(
		Source.World->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
	ASSERT_THAT(IsTrue(Before != SourceRoot));

	FSeinWorldSnapshot Snapshot;
	Source.World->CaptureSnapshot(Snapshot);
	ASSERT_THAT(AreEqual(
		FSeinWorldSnapshot::CurrentVersion,
		Snapshot.SnapshotVersion));

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
		Destination->RestoreSnapshot(Snapshot)));

	const FName PropertyNames[] = {
		TEXT("CurrentSteer"),
		TEXT("CurrentYawRate"),
		TEXT("CurrentSteer"),
	};
	const FFixedPoint Expected[] = {
		FFixedPoint::FromInt(11),
		FFixedPoint::FromInt(-7),
		FFixedPoint::FromInt(5),
	};
	for (int32 Index = 0; Index < Source.Entities.Num(); ++Index)
	{
		UObject* Restored =
			DestinationMovement->FindMovementInstance(
				Source.Entities[Index]);
		ASSERT_THAT(IsNotNull(Restored));
		FFixedPoint Actual;
		ASSERT_THAT(IsTrue(GetFixedState(
			*Restored, PropertyNames[Index], Actual)));
		ASSERT_THAT(IsTrue(Actual == Expected[Index]));
	}

	FGuid DestinationRoot;
	ASSERT_THAT(IsTrue(
		Destination->ComputeCanonicalStateRoot(
			DestinationRoot, Error)));
	ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

	FTSTicker::GetCoreTicker().Tick(
		Source.World->GetFixedDeltaTimeSeconds());
	ASSERT_THAT(IsTrue(
		Source.World->ComputeCanonicalStateRoot(
			SourceRoot, Error)));
	ASSERT_THAT(IsTrue(
		Destination->ComputeCanonicalStateRoot(
			DestinationRoot, Error)));
	ASSERT_THAT(IsTrue(SourceRoot == DestinationRoot));

	Source.World->StopSimulation();
	Destination->StopSimulation();
}

TEST(MovementPlusCoverageDuplicateGenerationIsReloadSafe,
	"SeinARTS.Unit.MovementPlus.CanonicalState")
{
	FMovementPlusCanonicalFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		TEXT("MovementPlusState.Reload"))));

	FSeinMovementStateCoverageDescriptor Descriptor;
	Descriptor.NativeClass =
		USeinWheeledVehicleMovement::StaticClass();
	Descriptor.Coverage =
		ESeinMovementStateCoverage::ReflectedComplete;
	FString Error;
	FSeinMovementStateCoverageRegistrationHandle Duplicate =
		FSeinMovementStateCoverageRegistry::Register(
			TEXT("SeinARTSMovementPlus"),
			Descriptor,
			&Error);
	ASSERT_THAT(IsTrue(Duplicate.IsValid()));

	FGuid Root;
	ASSERT_THAT(IsTrue(
		Fixture.World->ComputeCanonicalStateRoot(
			Root, Error)));
	Duplicate.Reset();
	ASSERT_THAT(IsTrue(
		Fixture.World->ComputeCanonicalStateRoot(
			Root, Error)));
	Fixture.World->StopSimulation();
}

TEST(MovementPlusUnloadClosesCoreAndSeversExtensionState,
	"SeinARTS.Unit.MovementPlus.CanonicalState")
{
	FActorTestSpawner Spawner;
	UWorld& UnrealWorld = Spawner.GetWorld();
	USeinWorldSubsystem* World =
		UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
	USeinMovementSubsystem* Movement =
		UnrealWorld.GetSubsystem<USeinMovementSubsystem>();
	ASSERT_THAT(IsNotNull(World));
	ASSERT_THAT(IsNotNull(Movement));

	FSeinEntityHandle BasicEntity;
	FSeinEntityHandle WheeledEntity;
	FString Error;
	ASSERT_THAT(IsTrue(
		SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				BasicEntity = World->SpawnAbstractEntity(
					FFixedTransform(),
					FSeinPlayerID::Neutral());
				FSeinMovementComponent Basic;
				Basic.MovementClass = FSoftClassPath(
					USeinBasicMovement::StaticClass()
						->GetPathName());
				World->AddComponent(BasicEntity, Basic);

				WheeledEntity = World->SpawnAbstractEntity(
					FFixedTransform(),
					FSeinPlayerID::Neutral());
				FSeinMovementComponent WheeledComponent;
				WheeledComponent.MovementClass = FSoftClassPath(
					USeinWheeledVehicleMovement::StaticClass()
						->GetPathName());
				WheeledComponent.MovementClassData =
					FInstancedStruct::Make(
						FSeinWheeledMovementData());
				World->AddComponent(
					WheeledEntity, WheeledComponent);
			},
			FSeinMatchSettings(),
			0x4D50554C,
			TEXT("MovementPlusState.Unload"),
			&Error)));
	ASSERT_THAT(IsTrue(
		SeinTestMatchBootstrap::Start(*World, &Error)));

	const FSeinMovementComponent* BasicComponent =
		World->GetComponent<FSeinMovementComponent>(
			BasicEntity);
	const FSeinMovementComponent* WheeledComponent =
		World->GetComponent<FSeinMovementComponent>(
			WheeledEntity);
	ASSERT_THAT(IsNotNull(BasicComponent));
	ASSERT_THAT(IsNotNull(WheeledComponent));
	ASSERT_THAT(IsNotNull(
		Movement->GetOrCreateMovementInstance(
			BasicEntity, *BasicComponent)));
	ASSERT_THAT(IsNotNull(
		Movement->GetOrCreateMovementInstance(
			WheeledEntity, *WheeledComponent)));
	ASSERT_THAT(AreEqual(
		2, Movement->GetMovementInstanceCount()));

	TestRunner->AddExpectedError(
		TEXT("Execution topology invalid:"),
		EAutomationExpectedErrorFlags::Contains, 1, false);
	Movement->ReleaseNativeClassStateForModuleUnload(
		TEXT("SeinARTSMovementPlus"));

	ASSERT_THAT(AreEqual(
		1, Movement->GetMovementInstanceCount()));
	ASSERT_THAT(IsNotNull(
		Movement->FindMovementInstance(BasicEntity)));
	ASSERT_THAT(IsNull(
		Movement->FindMovementInstance(WheeledEntity)));
	BasicComponent =
		World->GetComponent<FSeinMovementComponent>(
			BasicEntity);
	WheeledComponent =
		World->GetComponent<FSeinMovementComponent>(
			WheeledEntity);
	ASSERT_THAT(IsNull(BasicComponent));
	ASSERT_THAT(IsNull(WheeledComponent));
	ASSERT_THAT(IsTrue(
		World->IsTerminalAfterModuleUnload()));
	ASSERT_THAT(IsFalse(World->IsSimulationRunning()));
	ASSERT_THAT(IsFalse(
		World->IsExecutionTopologyValid()));
}
