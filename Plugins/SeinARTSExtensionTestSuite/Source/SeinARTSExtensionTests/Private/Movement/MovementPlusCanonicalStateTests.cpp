#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinMovementComponent.h"
#include "Core/SeinSystemPriority.h"
#include "Data/SeinFlyingMovementData.h"
#include "Data/SeinTrackedMovementData.h"
#include "Data/SeinWheeledMovementData.h"
#include "Data/SeinWorldSnapshot.h"
#include "Lib/SeinMovementPlusBPFL.h"
#include "Movement/SeinBasicMovement.h"
#include "Movement/SeinFlightMovement.h"
#include "Movement/SeinTrackedVehicleMovement.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "SeinMovementSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UnrealType.h"

struct FSeinMovementSubsystemTestAccess
{
	static void DropMovementInstance(
		USeinMovementSubsystem& Subsystem,
		FSeinEntityHandle Entity)
	{
		USeinMovement** Instance =
			Subsystem.MovementInstanceMap.Find(Entity);
		if (!Instance)
		{
			return;
		}
		Subsystem.MovementInstancePool.RemoveSingleSwap(*Instance);
		Subsystem.MovementInstanceMap.Remove(Entity);
		Subsystem.MovementStateRevisions.Remove(Entity);
		Subsystem.BumpMovementTopologyRevision();
	}
};

namespace
{
	class FDropMovementInstanceBeforePresentationSystem final
		: public ISeinSystem
	{
	public:
		void Configure(
			USeinMovementSubsystem* InMovement,
			FSeinEntityHandle InEntity)
		{
			Movement = InMovement;
			Entity = InEntity;
		}

		virtual void Tick(
			FFixedPoint,
			USeinWorldSubsystem&) override
		{
			if (USeinMovementSubsystem* Subsystem = Movement.Get())
			{
				FSeinMovementSubsystemTestAccess::DropMovementInstance(
					*Subsystem, Entity);
			}
		}

		virtual FSeinSystemDescriptor DescribeSystem() const override
		{
			return FSeinSystemDescriptor::Stateless(
				TEXT("seinarts.tests.movement_plus.drop_instance"),
				1u,
				ESeinTickPhase::FinalObservation,
				SeinSystemPriority::MovementPresentation - 1);
		}

	private:
		TWeakObjectPtr<USeinMovementSubsystem> Movement;
		FSeinEntityHandle Entity;
	};

	class FSettledTelemetryTransformSystem final : public ISeinSystem
	{
	public:
		void SetEntity(FSeinEntityHandle InEntity)
		{
			Entity = InEntity;
		}

		virtual void Tick(
			FFixedPoint,
			USeinWorldSubsystem& World) override
		{
			FSeinEntity* Mutable = World.GetEntityMutable(Entity);
			if (!Mutable)
			{
				return;
			}
			Mutable->Transform.SetLocation(
				Mutable->Transform.GetLocation()
					+ FFixedVector(
						FFixedPoint::FromInt(100),
						FFixedPoint::Zero,
						FFixedPoint::Zero));
		}

		virtual FSeinSystemDescriptor DescribeSystem() const override
		{
			return FSeinSystemDescriptor::Stateless(
				TEXT("seinarts.tests.movement_plus.settled_transform"),
				1u,
				ESeinTickPhase::PostTick,
				MAX_int32);
		}

	private:
		FSeinEntityHandle Entity;
	};

	struct FMovementPlusCanonicalFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		USeinMovementSubsystem* Movement = nullptr;
		TArray<FSeinEntityHandle> Entities;

		bool Initialize(
			FName FixtureId,
			ISeinSystem* AdditionalSystem = nullptr)
		{
			World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			Movement =
				Spawner.GetWorld().GetSubsystem<USeinMovementSubsystem>();
			if (!World || !Movement)
			{
				return false;
			}
			if (AdditionalSystem
				&& !World->RegisterSystem(AdditionalSystem))
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
		SeinTestSnapshotRestore::RestoreTrusted(
			*Destination, Snapshot)));

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

TEST(MovementPlusPresentationTelemetryIsTypedAndRenderOnly,
	"SeinARTS.Unit.MovementPlus.Telemetry")
{
	FMovementPlusCanonicalFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		TEXT("MovementPlusTelemetry.Typed"))));
	ASSERT_THAT(AreEqual(3, Fixture.Entities.Num()));

	const FSeinEntityHandle WheeledEntity = Fixture.Entities[0];
	const int32 HashBefore = Fixture.World->ComputeStateHash();
	FString RootError;
	FGuid RootBefore;
	ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
		RootBefore, RootError)));
	using namespace UE::SeinARTSMovementPlus::Telemetry;
	ASSERT_THAT(IsTrue(Clamp01(-FFixedPoint::One) == FFixedPoint::Zero));
	ASSERT_THAT(IsTrue(Clamp01(FFixedPoint::Half) == FFixedPoint::Half));
	ASSERT_THAT(IsTrue(Clamp01(FFixedPoint::FromInt(2)) == FFixedPoint::One));
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinMovementComponent* Movement =
			Fixture.World->GetComponentMutable<FSeinMovementComponent>(
				WheeledEntity);
		ASSERT_THAT(IsNotNull(Movement));
		SetRenderValue(
			*Movement, SteeringAngleSlot,
			FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10));
		SetRenderValue(
			*Movement, YawRateSlot,
			FFixedPoint::Half);
		SetRenderValue(
			*Movement, NormalizedThrottleSlot,
			FFixedPoint::One);
		SetRenderValue(
			*Movement, NormalizedBrakeSlot,
			FFixedPoint::Half);
		SetRenderValue(
			*Movement, WheelTravelDistanceSlot,
			FFixedPoint::FromInt(100));
		SetRenderValue(
			*Movement, SettledForwardSpeedSlot,
			FFixedPoint::FromInt(100));
	}

	ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
	FGuid RootAfterWrite;
	ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
		RootAfterWrite, RootError)));
	ASSERT_THAT(IsTrue(RootBefore == RootAfterWrite));
	FSeinMovementPlusPresentationDimensions Dimensions;
	Dimensions.WheelRadiusCm = 25.0f;
	Dimensions.TrackHalfWidthCm = 80.0f;
	const FSeinMovementPlusPresentationState State =
		USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
			&Fixture.Spawner.GetWorld(), WheeledEntity, Dimensions);
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		State.SteeringAngleRadians, 0.3f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		State.YawRateRadiansPerSecond, 0.5f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		State.NormalizedThrottle, 1.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		State.NormalizedBrake, 0.5f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		State.WheelRotationRadians, 4.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		State.LeftTrackVelocityCmPerSecond, 140.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		State.RightTrackVelocityCmPerSecond, 60.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		USeinMovementPlusBPFL::SeinGetMovementPlusTelemetryValue(
			&Fixture.Spawner.GetWorld(),
			WheeledEntity,
			ESeinMovementPlusTelemetryChannel::WheelRotation,
			Dimensions),
		4.0f,
		0.001f)));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinMovementComponent* Movement =
			Fixture.World->GetComponentMutable<FSeinMovementComponent>(
				WheeledEntity);
		ASSERT_THAT(IsNotNull(Movement));
		SetRenderValue(
			*Movement,
			WheelTravelDistanceSlot,
			FFixedPoint::FromInt(1000000000));
	}
	const FSeinMovementPlusPresentationState LongRunState =
		USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
			&Fixture.Spawner.GetWorld(), WheeledEntity, Dimensions);
	ASSERT_THAT(IsTrue(FMath::IsFinite(
		LongRunState.WheelRotationRadians)));
	ASSERT_THAT(IsTrue(LongRunState.WheelRotationRadians >= 0.0f));
	ASSERT_THAT(IsTrue(
		LongRunState.WheelRotationRadians < 2.0f * PI));
	constexpr double TwoPi = 6.28318530717958647692;
	const float ExpectedLongRunPhase = static_cast<float>(FMath::Fmod(
		1000000000.0 / 25.0,
		TwoPi));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		LongRunState.WheelRotationRadians,
		ExpectedLongRunPhase,
		0.00001f)));
	const FFixedPoint PresentationTravelLimit =
		FFixedPoint::FromInt(1000000000);
	ASSERT_THAT(IsTrue(AccumulateWheelTravel(
		PresentationTravelLimit,
		FFixedPoint::One) == FFixedPoint::One));
	ASSERT_THAT(IsTrue(AccumulateWheelTravel(
		-PresentationTravelLimit,
		-FFixedPoint::One) == -FFixedPoint::One));
	const FFixedPoint QuarterPresentationTravel =
		FFixedPoint::FromInt(250000000);
	FFixedPoint RepeatedTravel = FFixedPoint::Zero;
	for (int32 Step = 1; Step <= 4; ++Step)
	{
		RepeatedTravel = AccumulateWheelTravel(
			RepeatedTravel,
			QuarterPresentationTravel);
		ASSERT_THAT(IsTrue(RepeatedTravel == FFixedPoint::FromInt(
			Step * 250000000)));
	}
	RepeatedTravel = AccumulateWheelTravel(
		RepeatedTravel,
		QuarterPresentationTravel);
	ASSERT_THAT(IsTrue(RepeatedTravel == QuarterPresentationTravel));

	FSeinMovementPlusPresentationDimensions InvalidDimensions;
	InvalidDimensions.WheelRadiusCm = 0.0f;
	InvalidDimensions.TrackHalfWidthCm = -1.0f;
	const FSeinMovementPlusPresentationState InvalidDimensionState =
		USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
			&Fixture.Spawner.GetWorld(),
			WheeledEntity,
			InvalidDimensions);
	ASSERT_THAT(IsTrue(FMath::IsFinite(
		InvalidDimensionState.WheelRotationRadians)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		InvalidDimensionState.WheelRotationRadians)));
	ASSERT_THAT(IsTrue(FMath::IsFinite(
		InvalidDimensionState.LeftTrackVelocityCmPerSecond)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		InvalidDimensionState.LeftTrackVelocityCmPerSecond)));
	ASSERT_THAT(IsTrue(FMath::IsFinite(
		InvalidDimensionState.RightTrackVelocityCmPerSecond)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		InvalidDimensionState.RightTrackVelocityCmPerSecond)));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinMovementComponent* Movement =
			Fixture.World->GetComponentMutable<FSeinMovementComponent>(
				WheeledEntity);
		ASSERT_THAT(IsNotNull(Movement));
		UE::SeinARTSMovementPlus::Telemetry::
			ResetMovementPlusRenderValues(*Movement);
	}
	const FSeinMovementPlusPresentationState Reset =
		USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
			&Fixture.Spawner.GetWorld(), WheeledEntity, Dimensions);
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		Reset.SteeringAngleRadians, 0.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		Reset.YawRateRadiansPerSecond, 0.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		Reset.NormalizedThrottle, 0.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		Reset.NormalizedBrake, 0.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		Reset.WheelRotationRadians, 0.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		Reset.LeftTrackVelocityCmPerSecond, 0.0f, 0.001f)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyEqual(
		Reset.RightTrackVelocityCmPerSecond, 0.0f, 0.001f)));
	ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
	FGuid RootAfterReset;
	ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
		RootAfterReset, RootError)));
	ASSERT_THAT(IsTrue(RootBefore == RootAfterReset));

	Fixture.World->StopSimulation();
}

TEST(MovementPlusSettledTelemetryUsesFinalTransforms,
	"SeinARTS.Unit.MovementPlus.Telemetry")
{
	FMovementPlusCanonicalFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		TEXT("MovementPlusTelemetry.Settled"))));

	const TArray<FString> SystemOrder =
		Fixture.World->GetRegisteredSystemOrderForTests();
	const int32 ContainmentIndex = SystemOrder.IndexOfByKey(
		TEXT("seinarts.movement.nav_containment"));
	const int32 PresentationIndex = SystemOrder.IndexOfByKey(
		TEXT("seinarts.movement.presentation"));
	const int32 TraceIndex = SystemOrder.IndexOfByKey(
		TEXT("seinarts.movement.trace"));
	ASSERT_THAT(IsTrue(ContainmentIndex >= 0));
	ASSERT_THAT(IsTrue(PresentationIndex > ContainmentIndex));
	ASSERT_THAT(IsTrue(TraceIndex > PresentationIndex));

	const int32 HashBefore = Fixture.World->ComputeStateHash();
	FGuid RootBefore;
	FString RootError;
	ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
		RootBefore, RootError)));

	const FFixedPoint Half = FFixedPoint::Half;
	const FFixedQuaternion HalfRadianYaw =
		FFixedQuaternion::MakeFromEulers(
			FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, Half));
	const FFixedTransform PreviousTransform = FFixedTransform::Identity();
	const FFixedTransform CurrentTransform(
		FFixedVector(FFixedPoint::FromInt(100), FFixedPoint::Zero,
			FFixedPoint::Zero),
		HalfRadianYaw);
	FSeinSettledMovementRenderContext Context;
	Context.PreviousTransform = PreviousTransform;
	Context.CurrentTransform = CurrentTransform;
	Context.PreviousSettledVelocity = FFixedVector::ZeroVector;
	Context.SettledVelocity = FFixedVector(
		FFixedPoint::FromInt(100), FFixedPoint::Zero,
		FFixedPoint::Zero);
	Context.PreviousDriverVelocity = FFixedVector::ZeroVector;
	Context.DriverVelocity = FFixedVector(
		FFixedPoint::FromInt(100), FFixedPoint::Zero,
		FFixedPoint::Zero);
	Context.DeltaTime = FFixedPoint::One;
	Context.bHasPreviousSample = true;

	using namespace UE::SeinARTSMovementPlus::Telemetry;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinMovementComponent* WheeledData =
			Fixture.World->GetComponentMutable<FSeinMovementComponent>(
				Fixture.Entities[0]);
		FSeinMovementComponent* TrackedData =
			Fixture.World->GetComponentMutable<FSeinMovementComponent>(
				Fixture.Entities[1]);
		USeinWheeledVehicleMovement* Wheeled =
			Cast<USeinWheeledVehicleMovement>(Fixture.Instance(0));
		USeinTrackedVehicleMovement* Tracked =
			Cast<USeinTrackedVehicleMovement>(Fixture.Instance(1));
		ASSERT_THAT(IsNotNull(WheeledData));
		ASSERT_THAT(IsNotNull(TrackedData));
		ASSERT_THAT(IsNotNull(Wheeled));
		ASSERT_THAT(IsNotNull(Tracked));

		FSeinMovementRenderStateWriter WheeledWriter(
			WheeledData->RenderState);
		FSeinMovementRenderStateWriter TrackedWriter(
			TrackedData->RenderState);
		Wheeled->UpdateSettledRenderState(
			Context, *WheeledData, WheeledWriter);
		Tracked->UpdateSettledRenderState(
			Context, *TrackedData, TrackedWriter);

		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[YawRateSlot] > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[NormalizedThrottleSlot]
				> FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[NormalizedThrottleSlot]
				<= FFixedPoint::One));
		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[NormalizedBrakeSlot]
				== FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[WheelTravelDistanceSlot]
				> FFixedPoint::Zero));
		FSeinMovementPlusPresentationDimensions Dimensions;
		Dimensions.WheelRadiusCm = 25.0f;
		Dimensions.TrackHalfWidthCm = 80.0f;
		const FSeinMovementPlusPresentationState TrackedState =
			USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
				&Fixture.Spawner.GetWorld(),
				Fixture.Entities[1],
				Dimensions);
		ASSERT_THAT(IsTrue(
			TrackedState.LeftTrackVelocityCmPerSecond
				> TrackedState.RightTrackVelocityCmPerSecond));

		const FFixedPoint TravelBeforeNewOrder =
			WheeledWriter.GetValue(WheelTravelDistanceSlot);
		FSeinEntity* WheeledEntity =
			Fixture.World->GetEntityMutable(Fixture.Entities[0]);
		ASSERT_THAT(IsNotNull(WheeledEntity));
		FSeinPath EmptyPath;
		int32 WaypointIndex = 0;
		FSeinMovementContext MoveContext{
			*WheeledEntity,
			WheeledData,
			nullptr,
			EmptyPath,
			WaypointIndex,
			FFixedPoint::Zero,
			FFixedPoint::One,
			nullptr,
			Fixture.World,
			Fixture.Entities[0],
		};
		Wheeled->OnMoveBegin(MoveContext);
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(WheelTravelDistanceSlot)
				== TravelBeforeNewOrder));

		FSeinSettledMovementRenderContext Braking = Context;
		Braking.PreviousTransform = Context.CurrentTransform;
		Braking.CurrentTransform = FFixedTransform(
			FFixedVector(FFixedPoint::FromInt(150), FFixedPoint::Zero,
				FFixedPoint::Zero),
			HalfRadianYaw);
		Braking.PreviousSettledVelocity = Context.SettledVelocity;
		Braking.SettledVelocity = FFixedVector(
			FFixedPoint::FromInt(50), FFixedPoint::Zero,
			FFixedPoint::Zero);
		Braking.PreviousDriverVelocity = Context.DriverVelocity;
		Braking.DriverVelocity = FFixedVector(
			FFixedPoint::FromInt(50), FFixedPoint::Zero,
			FFixedPoint::Zero);
		Wheeled->UpdateSettledRenderState(
			Braking, *WheeledData, WheeledWriter);
		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[NormalizedThrottleSlot]
				== FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[NormalizedBrakeSlot]
				> FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledData->RenderState[NormalizedBrakeSlot]
				<= FFixedPoint::One));

		const FFixedPoint Tenth = FFixedPoint::One
			/ FFixedPoint::FromInt(10);
		FSeinSettledMovementRenderContext ReverseWrap = Context;
		ReverseWrap.PreviousTransform = FFixedTransform(
			FFixedVector::ZeroVector,
			FFixedQuaternion::MakeFromEulers(FFixedVector(
				FFixedPoint::Zero,
				FFixedPoint::Zero,
				FFixedPoint::Pi - Tenth)));
		ReverseWrap.CurrentTransform = FFixedTransform(
			FFixedVector(FFixedPoint::FromInt(100),
				FFixedPoint::Zero, FFixedPoint::Zero),
			FFixedQuaternion::MakeFromEulers(FFixedVector(
				FFixedPoint::Zero,
				FFixedPoint::Zero,
				-FFixedPoint::Pi + Tenth)));
		ReverseWrap.PreviousSettledVelocity = FFixedVector::ZeroVector;
		ReverseWrap.SettledVelocity = FFixedVector(
			FFixedPoint::FromInt(100),
			FFixedPoint::Zero,
			FFixedPoint::Zero);
		const FFixedPoint TravelBeforeReverse =
			WheeledWriter.GetValue(WheelTravelDistanceSlot);
		Wheeled->UpdateSettledRenderState(
			ReverseWrap, *WheeledData, WheeledWriter);
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(YawRateSlot) > FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(YawRateSlot) < FFixedPoint::One));
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(SettledForwardSpeedSlot)
				< FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(WheelTravelDistanceSlot)
				< TravelBeforeReverse));

		FSeinSettledMovementRenderContext FirstSample;
		Wheeled->UpdateSettledRenderState(
			FirstSample, *WheeledData, WheeledWriter);
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(YawRateSlot) == FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(NormalizedThrottleSlot)
				== FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(NormalizedBrakeSlot)
				== FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			WheeledWriter.GetValue(WheelTravelDistanceSlot)
				== FFixedPoint::Zero));
	}

	ASSERT_THAT(AreEqual(HashBefore, Fixture.World->ComputeStateHash()));
	FGuid RootAfter;
	ASSERT_THAT(IsTrue(Fixture.World->ComputeCanonicalStateRoot(
		RootAfter, RootError)));
	ASSERT_THAT(IsTrue(RootBefore == RootAfter));
	Fixture.World->StopSimulation();
}

TEST(MovementPlusProductionSamplerResetsAfterRestore,
	"SeinARTS.Determinism.MovementPlus.Telemetry")
{
	FSettledTelemetryTransformSystem FinalTransformSystem;
	FMovementPlusCanonicalFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		TEXT("MovementPlusTelemetry.ProductionSampler"),
		&FinalTransformSystem)));
	FinalTransformSystem.SetEntity(Fixture.Entities[0]);

	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	const FSeinMovementComponent* Movement =
		Fixture.World->GetComponent<FSeinMovementComponent>(
			Fixture.Entities[0]);
	ASSERT_THAT(IsNotNull(Movement));
	using namespace UE::SeinARTSMovementPlus::Telemetry;
	ASSERT_THAT(IsTrue(
		Movement->RenderState.IsValidIndex(WheelTravelDistanceSlot)));
	ASSERT_THAT(IsTrue(
		Movement->RenderState[WheelTravelDistanceSlot] > FFixedPoint::Zero));
	ASSERT_THAT(IsTrue(
		Movement->RenderState[NormalizedThrottleSlot]
			== FFixedPoint::Zero));

	FSeinWorldSnapshot Snapshot;
	Fixture.World->CaptureSnapshot(Snapshot);
	Fixture.World->StopSimulation();
	FString Error;
	ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
		*Fixture.World, Snapshot, &Error)));
	Movement = Fixture.World->GetComponent<FSeinMovementComponent>(
		Fixture.Entities[0]);
	ASSERT_THAT(IsNotNull(Movement));
	FSeinMovementPlusPresentationDimensions Dimensions;
	const FSeinMovementPlusPresentationState ImmediateAfterRestore =
		USeinMovementPlusBPFL::SeinGetMovementPlusPresentationState(
			&Fixture.Spawner.GetWorld(),
			Fixture.Entities[0],
			Dimensions);
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		ImmediateAfterRestore.SteeringAngleRadians)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		ImmediateAfterRestore.YawRateRadiansPerSecond)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		ImmediateAfterRestore.NormalizedThrottle)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		ImmediateAfterRestore.NormalizedBrake)));
	ASSERT_THAT(IsTrue(FMath::IsNearlyZero(
		ImmediateAfterRestore.WheelRotationRadians)));
	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	Movement = Fixture.World->GetComponent<FSeinMovementComponent>(
		Fixture.Entities[0]);
	ASSERT_THAT(IsNotNull(Movement));
	ASSERT_THAT(IsTrue(
		Movement->RenderState.IsValidIndex(SteeringAngleSlot)));
	ASSERT_THAT(IsTrue(
		!Movement->RenderState.IsValidIndex(WheelTravelDistanceSlot)
		|| Movement->RenderState[WheelTravelDistanceSlot]
			== FFixedPoint::Zero));
	ASSERT_THAT(IsTrue(
		!Movement->RenderState.IsValidIndex(NormalizedThrottleSlot)
		|| Movement->RenderState[NormalizedThrottleSlot]
			== FFixedPoint::Zero));

	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinMovementComponent* MutableMovement =
			Fixture.World->GetComponentMutable<FSeinMovementComponent>(
				Fixture.Entities[0]);
		ASSERT_THAT(IsNotNull(MutableMovement));
		FSeinMovementRenderStateWriter Writer(
			MutableMovement->RenderState);
		Writer.SetValue(WheelTravelDistanceSlot, FFixedPoint::One);
		ASSERT_THAT(IsTrue(
			MutableMovement->RenderState[WheelTravelDistanceSlot]
				== FFixedPoint::One));
		MutableMovement->MovementClass = FSoftClassPath(
			USeinBasicMovement::StaticClass()->GetPathName());
	}
	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	Movement = Fixture.World->GetComponent<FSeinMovementComponent>(
		Fixture.Entities[0]);
	ASSERT_THAT(IsNotNull(Movement));
	ASSERT_THAT(IsTrue(Movement->RenderState.IsEmpty()));
	Fixture.World->StopSimulation();
}

TEST(MovementPlusPresentationClearsWhenMovementInstanceDisappears,
	"SeinARTS.Unit.MovementPlus.Telemetry")
{
	FDropMovementInstanceBeforePresentationSystem DropSystem;
	FMovementPlusCanonicalFixture Fixture;
	ASSERT_THAT(IsTrue(Fixture.Initialize(
		TEXT("MovementPlusTelemetry.MissingInstance"),
		&DropSystem)));
	DropSystem.Configure(Fixture.Movement, Fixture.Entities[0]);

	using namespace UE::SeinARTSMovementPlus::Telemetry;
	{
		auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
		FSeinMovementComponent* Movement =
			Fixture.World->GetComponentMutable<FSeinMovementComponent>(
				Fixture.Entities[0]);
		ASSERT_THAT(IsNotNull(Movement));
		FSeinMovementRenderStateWriter Writer(Movement->RenderState);
		Writer.SetValue(WheelTravelDistanceSlot, FFixedPoint::One);
		Writer.SetValue(NormalizedThrottleSlot, FFixedPoint::One);
	}

	FTSTicker::GetCoreTicker().Tick(
		Fixture.World->GetFixedDeltaTimeSeconds());
	const FSeinMovementComponent* Movement =
		Fixture.World->GetComponent<FSeinMovementComponent>(
			Fixture.Entities[0]);
	ASSERT_THAT(IsNotNull(Movement));
	ASSERT_THAT(IsTrue(Movement->RenderState.IsEmpty()));
	ASSERT_THAT(IsNull(Fixture.Movement->FindMovementInstance(
		Fixture.Entities[0])));
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
