#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actions/SeinMoveToAction.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "HAL/PlatformTime.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Movement/SeinBasicUnitMovement.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinLevelDataTestTypes.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool TickSimulation(USeinWorldSubsystem& World, float DeltaTime)
	{
		return World.TickSimulation(DeltaTime);
	}
};

namespace UE::SeinARTSTests
{
	namespace CombatScaleTestLocal
	{
		// Open-field grid: GridSize x GridSize cells at 100 units. Two blocks
		// of Population/2 units face each other and cross through the middle,
		// so the measured ticks carry pathing consumption, steering, PreTick
		// avoidance, PostTick collision, and containment simultaneously.
		constexpr int32 GridSize = 96;
		constexpr int32 CellSize = 100;
		constexpr int32 UnitRadius = 40;
		constexpr int32 UnitSpacing = 90;
		constexpr int32 TimedSamples = 9;

		void ConfigureOpenField(USeinLevelDataTestDouble& LevelData)
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
			const int32 BlockDepth = 2000;
			const int32 X = bEastBlock
				? FieldSpan - BlockDepth + Row * UnitSpacing
				: BlockDepth - Row * UnitSpacing;
			return FFixedVector(
				FFixedPoint::FromInt(X),
				FFixedPoint::FromInt(1000 + Col * UnitSpacing),
				FFixedPoint::Zero);
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
				OutError = TEXT("Combat scale world lacked a sim or navigation.");
				return false;
			}

			USeinLevelDataTestDouble* Field =
				NewObject<USeinLevelDataTestDouble>();
			ConfigureOpenField(*Field);
			if (!Navigation->LoadFromSubstrate(*Field).IsAdopted())
			{
				OutError = TEXT("Combat scale navigation grid was not adopted.");
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
					Shape.Radius = FFixedPoint::FromInt(UnitRadius);
					Shape.Height = FFixedPoint::FromInt(180);
					FSeinExtentsComponent Extents;
					Extents.Shapes.Add(Shape);
					Extents.bCollisionEnabled = true;
					Extents.Mobility = ESeinCollisionMobility::Movable;
					Extents.Mass = FFixedPoint::FromInt(100);
					Extents.ObjectType.Channel = FName(TEXT("Default"));
					World->AddComponent(Handle, Extents);

					FSeinMovementComponent Movement;
					Movement.MovementClass = FSoftClassPath(
						USeinBasicUnitMovement::StaticClass()->GetPathName());
					World->AddComponent(Handle, Movement);
					World->AddComponent(Handle, FSeinNavigationComponent());
					World->AddComponent(Handle, FSeinAbilityComponent());
					AbilityIDs.Add(USeinAbilityBPFL::SeinGrantAbility(
						World, Handle,
						USeinMoveToLifecycleTestAbility::StaticClass()));
					Handles.Add(Handle);
				}
			};

			if (!SeinTestMatchBootstrap::Materialize(
					*World,
					AuthorState,
					FSeinMatchSettings(),
					0x434D4254,
					TEXT("SeinARTS.CombatScale"),
					&OutError)
				|| !bAuthoringSucceeded
				|| Handles.Num() != Population
				|| !SeinTestMatchBootstrap::Start(*World, &OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("Could not materialize the combat scale workload.");
				}
				return false;
			}

			USeinLatentActionManager* Manager = World->LatentActionManager;
			if (!Manager)
			{
				OutError = TEXT("Combat scale world lacked a latent manager.");
				return false;
			}

			// Order every unit across the field through its own real Move To
			// action, owned by a real activated ability.
			{
				auto SimScope = FSeinSimContextTestAccess::Enter(*World);
				for (int32 Index = 0; Index < Population; ++Index)
				{
					USeinAbility* Ability =
						World->GetAbilityInstance(AbilityIDs[Index]);
					if (!Ability
						|| !Ability->ActivateAbility(
							FSeinEntityHandle::Invalid(),
							FFixedVector::ZeroVector))
					{
						OutError = TEXT("Combat scale ability activation failed.");
						World->StopSimulation();
						return false;
					}
					const bool bEast = Index >= BlockCount;
					const int32 InBlock =
						bEast ? Index - BlockCount : Index;
					// Destination: the mirrored slot in the OPPOSITE block, so
					// the two armies pass through each other mid-field.
					const FFixedVector Destination =
						BlockPosition(InBlock, Columns, !bEast);
					USeinMoveToProxy* Proxy =
						NewObject<USeinMoveToProxy>(World);
					USeinMoveToAction* Action =
						NewObject<USeinMoveToAction>(Proxy);
					Action->OwningAbility = Ability;
					Action->OwnerEntity = Handles[Index];
					Action->Observer = Proxy;
					Action->Initialize(Destination);
					Manager->RegisterAction(Action);
				}
			}

			// Warmup: let the budgeted path scheduler serve the whole
			// population and bring both blocks into contact.
			const int32 WarmupTicks = 40 + Population / 24;
			for (int32 Tick = 0; Tick < WarmupTicks; ++Tick)
			{
				if (!FSeinWorldSubsystemTestAccess::TickSimulation(
						*World, World->GetFixedDeltaTimeSeconds()))
				{
					OutError = TEXT("Combat scale warmup lost the scheduler.");
					World->StopSimulation();
					return false;
				}
			}

			// Vacuity guard: the armies must genuinely be moving.
			int32 MovedUnits = 0;
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
					++MovedUnits;
				}
			}
			if (MovedUnits * 2 < Population)
			{
				OutError = FString::Printf(
					TEXT("Combat scale workload was vacuous: %d/%d units moved."),
					MovedUnits, Population);
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
					OutError = TEXT("Combat scale sample did not advance exactly one tick.");
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

	TEST(MovingCombatFixedTickHasMeasuredPopulationCurve,
		"SeinARTS.Perf.Combat.Scale")
	{
		using namespace CombatScaleTestLocal;
		const int32 Populations[] = {300, 500, 1000};
		double Medians[UE_ARRAY_COUNT(Populations)] = {};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Populations); ++Index)
		{
			FString Error;
			ASSERT_THAT(IsTrue(MeasurePopulation(
				Populations[Index], Medians[Index], Error)));
			UE_LOG(LogTemp, Display,
				TEXT("Moving combat fixed tick median at %d units: %.3f ms"),
				Populations[Index], Medians[Index]);
		}

		// Anti-catastrophe ceiling, not a target: the value of this test is
		// the measured curve above. Tighten once a machine baseline exists.
		ASSERT_THAT(IsTrue(Medians[2] < 150.0));
	}
}
