/**
 * SeinARTS Extension Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    MovementPlusScaleTests.cpp
 * @brief   Fixed-tick scale curve for Movement+ vehicles: two mixed
 *          wheeled/tracked columns cross an open field through real A*
 *          pathing, maneuver planning, steering, avoidance, and collision.
 *          The measured medians are the product; the ceiling assert is an
 *          anti-catastrophe backstop, not a target.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Data/SeinTrackedMovementData.h"
#include "Data/SeinWheeledMovementData.h"
#include "HAL/PlatformTime.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Movement/SeinTrackedVehicleMovement.h"
#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinLevelData.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "SeinVehicleGymTestTypes.h"
#include "MovementPlusScaleTestTypes.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool TickSimulation(USeinWorldSubsystem& World, float DeltaTime)
	{
		return World.TickSimulation(DeltaTime);
	}
};

namespace UE::SeinARTSTests
{
	namespace MovementPlusScaleTestLocal
	{
		// Vehicles are large: a wider field and coarser spacing than the
		// infantry combat-scale workload, so columns can actually maneuver.
		constexpr int32 GridSize = 128;
		constexpr int32 CellSize = 100;
		constexpr int32 VehicleRadius = 90;
		constexpr int32 VehicleSpacing = 260;
		constexpr int32 TimedSamples = 9;

		void ConfigureOpenField(USeinMovementPlusScaleLevelData& LevelData)
		{
			const int32 NumCells = GridSize * GridSize;
			LevelData.TestDimensions = FIntPoint(GridSize, GridSize);
			LevelData.TestSurfaces.SetNumZeroed(NumCells);
			for (FSeinLevelCellSurface& Surface : LevelData.TestSurfaces)
			{
				Surface.bInBounds = true;
				Surface.bHasSurface = true;
				Surface.NormalZ = FFixedPoint::One;
			}
			TArray<uint8>& Channel =
				LevelData.LayerChannels.FindOrAdd(TEXT("Nav"));
			Channel.SetNumZeroed(2 * NumCells);
			for (int32 Index = 0; Index < NumCells; ++Index)
			{
				Channel[Index] = 1;                 // passable, unit cost
				Channel[NumCells + Index] = 0xFF;   // fully connected
			}
		}

		FFixedVector BlockPosition(
			int32 IndexInBlock, int32 Columns, bool bEastBlock)
		{
			const int32 Row = IndexInBlock / Columns;
			const int32 Col = IndexInBlock % Columns;
			const int32 FieldSpan = GridSize * CellSize;
			const int32 BlockDepth = 2400;
			const int32 X = bEastBlock
				? FieldSpan - BlockDepth + Row * VehicleSpacing
				: BlockDepth - Row * VehicleSpacing;
			return FFixedVector(
				FFixedPoint::FromInt(X),
				FFixedPoint::FromInt(1200 + Col * VehicleSpacing),
				FFixedPoint::Zero);
		}

		FSeinMovementComponent MakeVehicleMovement(
			bool bTracked, FSeinNavigationComponent& OutNavigation)
		{
			FSeinMovementComponent Movement;
			Movement.TopSpeed = FFixedPoint::FromInt(bTracked ? 450 : 650);
			Movement.TurnRate = FFixedPoint::FromInt(bTracked ? 1 : 2);
			Movement.ReverseTopSpeed = Movement.TopSpeed * FFixedPoint::Half;
			Movement.ReverseEngageDistanceThreshold =
				FFixedPoint::FromInt(600);
			if (bTracked)
			{
				Movement.MovementClass = FSoftClassPath(
					USeinTrackedVehicleMovement::StaticClass());
				FSeinTrackedMovementData Tracked;
				Tracked.MinTurnRadius = FFixedPoint::FromInt(350);
				Movement.MovementClassData = FInstancedStruct::Make(Tracked);
			}
			else
			{
				Movement.MovementClass = FSoftClassPath(
					USeinWheeledVehicleMovement::StaticClass());
				FSeinWheeledMovementData Wheeled;
				Wheeled.Wheelbase = FFixedPoint::FromInt(300);
				Movement.MovementClassData = FInstancedStruct::Make(Wheeled);
			}
			OutNavigation.FallbackFootprintRadius =
				FFixedPoint::FromInt(VehicleRadius);
			OutNavigation.AcceptanceRadius = FFixedPoint::FromInt(120);
			OutNavigation.RepathMode = ESeinRepathMode::OffPathOnly;
			OutNavigation.OffPathThreshold = FFixedPoint::FromInt(10000);
			return Movement;
		}

		bool MeasurePopulation(
			int32 Population,
			double& OutMedianMilliseconds,
			FString& OutError)
		{
			FActorTestSpawner Spawner;
			UWorld& UnrealWorld = Spawner.GetWorld();
			USeinWorldSubsystem* World =
				UnrealWorld.GetSubsystem<USeinWorldSubsystem>();
			USeinNavigation* Navigation =
				USeinNavigationSubsystem::GetNavigationForWorld(&UnrealWorld);
			if (!World || !Navigation)
			{
				OutError = TEXT("Vehicle scale world lacked a sim or navigation.");
				return false;
			}

			USeinMovementPlusScaleLevelData* Field =
				NewObject<USeinMovementPlusScaleLevelData>();
			ConfigureOpenField(*Field);
			if (!Navigation->LoadFromSubstrate(*Field).IsAdopted())
			{
				OutError = TEXT("Vehicle scale navigation grid was not adopted.");
				return false;
			}

			const int32 BlockCount = Population / 2;
			const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(
				static_cast<double>(BlockCount))));
			TArray<FSeinEntityHandle> Handles;
			TArray<int32> AbilityIDs;
			Handles.Reserve(Population);
			AbilityIDs.Reserve(Population);
			bool bAuthoringSucceeded = true;
			const auto AuthorState = [&]()
			{
				for (int32 Index = 0; Index < Population; ++Index)
				{
					const bool bEast = Index >= BlockCount;
					const int32 InBlock = bEast ? Index - BlockCount : Index;
					const FSeinEntityHandle Handle = World->SpawnAbstractEntity(
						FFixedTransform(BlockPosition(InBlock, Columns, bEast)),
						FSeinPlayerID::Neutral());
					if (!Handle.IsValid())
					{
						bAuthoringSucceeded = false;
						return;
					}

					FSeinExtentsShape Shape;
					Shape.Shape = ESeinExtentsShape::Capsule;
					Shape.Radius = FFixedPoint::FromInt(VehicleRadius);
					Shape.Height = FFixedPoint::FromInt(250);
					FSeinExtentsComponent Extents;
					Extents.Shapes.Add(Shape);
					Extents.bCollisionEnabled = true;
					Extents.Mobility = ESeinCollisionMobility::Movable;
					Extents.Mass = FFixedPoint::FromInt(8000);
					Extents.ObjectType.Channel = FName(TEXT("Default"));
					World->AddComponent(Handle, Extents);

					// Alternate wheeled and tracked so the measured tick
					// carries both maneuver planners at once.
					FSeinNavigationComponent NavigationComponent;
					const FSeinMovementComponent Movement =
						MakeVehicleMovement(
							(Index & 1) != 0, NavigationComponent);
					World->AddComponent(Handle, Movement);
					World->AddComponent(Handle, NavigationComponent);
					World->AddComponent(Handle, FSeinAbilityComponent());
					AbilityIDs.Add(USeinAbilityBPFL::SeinGrantAbility(
						World, Handle,
						USeinVehicleGymAbility::StaticClass()));
					Handles.Add(Handle);
				}
			};

			if (!SeinTestMatchBootstrap::Materialize(
					*World,
					AuthorState,
					FSeinMatchSettings(),
					0x4D565343,
					TEXT("SeinARTS.MovementPlusScale"),
					&OutError)
				|| !bAuthoringSucceeded
				|| Handles.Num() != Population
				|| !SeinTestMatchBootstrap::Start(*World, &OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("Could not materialize the vehicle scale workload.");
				}
				return false;
			}
			if (!World->LatentActionManager)
			{
				OutError = TEXT("Vehicle scale world lacked a latent manager.");
				return false;
			}

			// Order every vehicle across the field through the public Move To
			// proxy, owned by a real activated ability.
			{
				auto SimScope = FSeinSimContextTestAccess::Enter(*World);
				for (int32 Index = 0; Index < Population; ++Index)
				{
					USeinAbility* Ability =
						World->GetAbilityInstance(AbilityIDs[Index]);
					const bool bEast = Index >= BlockCount;
					const int32 InBlock =
						bEast ? Index - BlockCount : Index;
					const FFixedVector Destination =
						BlockPosition(InBlock, Columns, !bEast);
					if (!Ability
						|| !Ability->ActivateAbility(
							FSeinEntityHandle::Invalid(), Destination))
					{
						OutError = TEXT("Vehicle scale ability activation failed.");
						World->StopSimulation();
						return false;
					}
					USeinMoveToProxy* Proxy =
						USeinMoveToProxy::SeinMoveTo(Ability, Destination);
					if (!Proxy)
					{
						OutError = TEXT("Vehicle scale Move To creation failed.");
						World->StopSimulation();
						return false;
					}
					Proxy->Activate();
				}
			}

			// Warmup: serve the budgeted path scheduler and let the maneuver
			// planners resolve their start arcs before sampling.
			const int32 WarmupTicks = 60 + Population / 8;
			for (int32 Tick = 0; Tick < WarmupTicks; ++Tick)
			{
				if (!FSeinWorldSubsystemTestAccess::TickSimulation(
						*World, World->GetFixedDeltaTimeSeconds()))
				{
					OutError = TEXT("Vehicle scale warmup lost the scheduler.");
					World->StopSimulation();
					return false;
				}
			}

			// Vacuity guard: the columns must genuinely be rolling.
			int32 MovedVehicles = 0;
			for (int32 Index = 0; Index < Population; ++Index)
			{
				const bool bEast = Index >= BlockCount;
				const int32 InBlock = bEast ? Index - BlockCount : Index;
				const FSeinEntity* Entity = World->GetEntity(Handles[Index]);
				if (Entity
					&& !FFixedVector::IsPlanarDistanceWithin(
						Entity->Transform.GetLocation(),
						BlockPosition(InBlock, Columns, bEast),
						FFixedPoint::FromInt(CellSize)))
				{
					++MovedVehicles;
				}
			}
			if (MovedVehicles * 2 < Population)
			{
				OutError = FString::Printf(
					TEXT("Vehicle scale workload was vacuous: %d/%d vehicles moved."),
					MovedVehicles, Population);
				World->StopSimulation();
				return false;
			}

			TArray<double> Samples;
			Samples.Reserve(TimedSamples);
			for (int32 Sample = 0; Sample < TimedSamples; ++Sample)
			{
				const int32 TickBefore = World->GetCurrentTick();
				const double StartedAt = FPlatformTime::Seconds();
				const bool bSchedulerRetained =
					FSeinWorldSubsystemTestAccess::TickSimulation(
						*World, World->GetFixedDeltaTimeSeconds());
				const double ElapsedMilliseconds =
					(FPlatformTime::Seconds() - StartedAt) * 1000.0;
				if (!bSchedulerRetained
					|| World->GetCurrentTick() != TickBefore + 1)
				{
					OutError = TEXT("Vehicle scale sample did not advance exactly one tick.");
					World->StopSimulation();
					return false;
				}
				Samples.Add(ElapsedMilliseconds);
			}
			World->StopSimulation();

			Samples.Sort();
			OutMedianMilliseconds = Samples[Samples.Num() / 2];
			return true;
		}
	}

	TEST(MovementPlusVehiclesFixedTickHasMeasuredPopulationCurve,
		"SeinARTS.Perf.MovementPlus.Scale")
	{
		using namespace MovementPlusScaleTestLocal;
		const int32 Populations[] = {100, 200, 400};
		double Medians[UE_ARRAY_COUNT(Populations)] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Populations); ++Index)
		{
			FString Error;
			ASSERT_THAT(IsTrue(MeasurePopulation(
				Populations[Index], Medians[Index], Error)));
			UE_LOG(LogTemp, Display,
				TEXT("Movement+ vehicle fixed tick median at %d vehicles: %.3f ms"),
				Populations[Index], Medians[Index]);
		}

		// Anti-catastrophe ceiling, not a target: the value of this test is
		// the measured curve above. Tighten once a machine baseline exists.
		ASSERT_THAT(IsTrue(Medians[2] < 150.0));
	}
}
