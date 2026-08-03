#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"

#include "Abilities/SeinLatentActionManager.h"
#include "Abilities/SeinMoveToProxy.h"
#include "Actions/SeinMoveToAction.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Data/SeinTrackedMovementData.h"
#include "Data/SeinWheeledMovementData.h"
#include "Data/SeinWorldSnapshot.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Movement/SeinVehicleGymTestTypes.h"
#include "Movement/SeinInfantryMovement.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "SeinMovementSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"
#include "Types/Entity.h"
#include "Types/Quat.h"
#include "Types/Transform.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 DriverTickRate = 30;
	constexpr int32 MaxDriverTicks = DriverTickRate * 45;

	FFixedVector Point(int32 X, int32 Y = 0, int32 Z = 0)
	{
		return FFixedVector(
			FFixedPoint::FromInt(X),
			FFixedPoint::FromInt(Y),
			FFixedPoint::FromInt(Z));
	}

	FFixedTransform Pose(
		const FFixedVector& Location,
		FFixedPoint Yaw = FFixedPoint::Zero)
	{
		return FFixedTransform(
			Location,
			FFixedQuaternion::MakeFromEulers(FFixedVector(
				FFixedPoint::Zero,
				FFixedPoint::Zero,
				Yaw)));
	}

	bool PathHasArc(const FSeinPath& Path)
	{
		for (const FSeinPathSegment& Segment : Path.Segments)
		{
			if (Segment.Type == ESeinPathSegmentType::Arc)
			{
				return true;
			}
		}
		return false;
	}

	bool PathHasReverse(const FSeinPath& Path)
	{
		for (const FSeinPathSegment& Segment : Path.Segments)
		{
			if (Segment.bReverse)
			{
				return true;
			}
		}
		return false;
	}

	bool PathHasCusp(const FSeinPath& Path)
	{
		for (int32 Index = 1; Index < Path.Segments.Num(); ++Index)
		{
			if (Path.Segments[Index - 1].bReverse
				!= Path.Segments[Index].bReverse)
			{
				return true;
			}
		}
		return false;
	}

	bool PathHasLateralWaypoint(const FSeinPath& Path)
	{
		for (const FFixedVector& Waypoint : Path.Waypoints)
		{
			if (Waypoint.Y != FFixedPoint::Zero)
			{
				return true;
			}
		}
		return false;
	}

	bool SetFixedProperty(
		UObject& Object,
		FName PropertyName,
		FFixedPoint Value)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(
			Object.GetClass(), PropertyName);
		if (!Property
			|| Property->Struct != FFixedPoint::StaticStruct())
		{
			return false;
		}
		*Property->ContainerPtrToValuePtr<FFixedPoint>(&Object) = Value;
		return true;
	}

	bool SetIntProperty(
		UObject& Object,
		FName PropertyName,
		int32 Value)
	{
		FIntProperty* Property = FindFProperty<FIntProperty>(
			Object.GetClass(), PropertyName);
		if (!Property)
		{
			return false;
		}
		Property->SetPropertyValue_InContainer(&Object, Value);
		return true;
	}

	bool PathsEqual(const FSeinPath& A, const FSeinPath& B)
	{
		if (A.Waypoints != B.Waypoints
			|| A.Segments.Num() != B.Segments.Num()
			|| A.TotalCost != B.TotalCost
			|| A.bIsValid != B.bIsValid
			|| A.bIsPartial != B.bIsPartial)
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Segments.Num(); ++Index)
		{
			const FSeinPathSegment& Left = A.Segments[Index];
			const FSeinPathSegment& Right = B.Segments[Index];
			if (Left.Type != Right.Type
				|| Left.From != Right.From
				|| Left.To != Right.To
				|| Left.Center != Right.Center
				|| Left.Radius != Right.Radius
				|| Left.SweepAngle != Right.SweepAngle
				|| Left.bReverse != Right.bReverse)
			{
				return false;
			}
		}
		return true;
	}

	bool ValidateTypedPath(
		const FSeinPath& Path,
		const FFixedVector& ExpectedDestination,
		FString& OutError)
	{
		OutError.Reset();
		if (!Path.bIsValid || Path.Waypoints.IsEmpty()
			|| Path.Waypoints.Last() != ExpectedDestination)
		{
			OutError = TEXT("path is invalid or moved the exact destination");
			return false;
		}
		if (Path.TotalCost < FFixedPoint::Zero)
		{
			OutError = TEXT("path cost is negative");
			return false;
		}
		for (int32 Index = 0; Index < Path.Segments.Num(); ++Index)
		{
			const FSeinPathSegment& Segment = Path.Segments[Index];
			if (Index > 0
				&& Path.Segments[Index - 1].To != Segment.From)
			{
				OutError = FString::Printf(
					TEXT("segment chain breaks at %d"), Index);
				return false;
			}
			if (Segment.Type == ESeinPathSegmentType::Arc
				&& (Segment.Radius <= FFixedPoint::Zero
					|| Segment.SweepAngle == FFixedPoint::Zero))
			{
				OutError = FString::Printf(
					TEXT("arc %d has invalid radius or sweep"), Index);
				return false;
			}
		}
		if (!Path.Segments.IsEmpty()
			&& Path.Segments.Last().To != ExpectedDestination)
		{
			OutError = TEXT("typed path terminal does not equal destination");
			return false;
		}

		FSeinPath Flattened = Path;
		Flattened.FlattenToWaypoints(FFixedPoint::FromInt(5));
		if (Flattened.Waypoints.IsEmpty()
			|| Flattened.Waypoints.Last() != ExpectedDestination)
		{
			OutError = TEXT("arc flattening moved the exact destination");
			return false;
		}
		return true;
	}

	FSeinMovementComponent MakeWheeledComponent(
		int32 TopSpeed,
		int32 TurnRateNumerator,
		int32 TurnRateDenominator,
		int32 Wheelbase,
		int32 FootprintRadius,
		FSeinNavigationComponent& OutNavigation)
	{
		FSeinMovementComponent Movement;
		Movement.MovementClass = FSoftClassPath(
			USeinWheeledVehicleMovement::StaticClass());
		Movement.TopSpeed = FFixedPoint::FromInt(TopSpeed);
		Movement.TurnRate = FFixedPoint::FromInt(TurnRateNumerator)
			/ FFixedPoint::FromInt(TurnRateDenominator);
		Movement.ReverseTopSpeed = Movement.TopSpeed * FFixedPoint::Half;
		Movement.ReverseEngageDistanceThreshold = FFixedPoint::FromInt(600);
		FSeinWheeledMovementData Wheeled;
		Wheeled.Wheelbase = FFixedPoint::FromInt(Wheelbase);
		Movement.MovementClassData = FInstancedStruct::Make(Wheeled);
		OutNavigation.FallbackFootprintRadius =
			FFixedPoint::FromInt(FootprintRadius);
		OutNavigation.AcceptanceRadius = FFixedPoint::FromInt(80);
		OutNavigation.RepathMode = ESeinRepathMode::OffPathOnly;
		OutNavigation.OffPathThreshold = FFixedPoint::FromInt(10000);
		return Movement;
	}

	FSeinMovementComponent MakeTrackedComponent(
		int32 TopSpeed,
		int32 TurnRateNumerator,
		int32 TurnRateDenominator,
		int32 MinTurnRadius,
		int32 FootprintRadius,
		FSeinNavigationComponent& OutNavigation)
	{
		FSeinMovementComponent Movement;
		Movement.MovementClass = FSoftClassPath(
			USeinTrackedVehicleMovement::StaticClass());
		Movement.TopSpeed = FFixedPoint::FromInt(TopSpeed);
		Movement.TurnRate = FFixedPoint::FromInt(TurnRateNumerator)
			/ FFixedPoint::FromInt(TurnRateDenominator);
		Movement.ReverseTopSpeed = Movement.TopSpeed * FFixedPoint::Half;
		Movement.ReverseEngageDistanceThreshold = FFixedPoint::FromInt(600);
		FSeinTrackedMovementData Tracked;
		Tracked.MinTurnRadius = FFixedPoint::FromInt(MinTurnRadius);
		Movement.MovementClassData = FInstancedStruct::Make(Tracked);
		OutNavigation.FallbackFootprintRadius =
			FFixedPoint::FromInt(FootprintRadius);
		OutNavigation.AcceptanceRadius = FFixedPoint::FromInt(80);
		OutNavigation.RepathMode = ESeinRepathMode::OffPathOnly;
		OutNavigation.OffPathThreshold = FFixedPoint::FromInt(10000);
		return Movement;
	}

	template <typename TPlanner>
	bool PlanCoarseRoute(
		TPlanner& Planner,
		const TArray<FFixedVector>& Route,
		FSeinEntity& Entity,
		const FSeinMovementComponent& Movement,
		const FSeinNavigationComponent& Navigation,
		USeinNavigation* NavigationPolicy,
		FSeinPath& OutPath)
	{
		if (Route.IsEmpty())
		{
			return false;
		}
		Planner.CoarseRoute = Route;
		const FSeinPlanPathContext Context{
			Entity,
			&Movement,
			&Navigation,
			Route.Last(),
			NavigationPolicy,
			nullptr,
			FSeinEntityHandle(1, 1),
			nullptr,
		};
		return Planner.PlanPath(Context, OutPath)
			== ESeinPathResult::Found;
	}

	struct FDriverFrame
	{
		FFixedTransform Transform;
		FFixedVector Velocity;
		int32 Waypoint = 0;
		bool bCompleted = false;

		bool operator==(const FDriverFrame& Other) const
		{
			return Transform.Location == Other.Transform.Location
				&& Transform.Rotation == Other.Transform.Rotation
				&& Transform.Scale == Other.Transform.Scale
				&& Velocity == Other.Velocity
				&& Waypoint == Other.Waypoint
				&& bCompleted == Other.bCompleted;
		}
	};

	struct FDriverRun
	{
		FSeinPath PlannedPath;
		TArray<FDriverFrame> Frames;
		bool bCompleted = false;
		bool bSawReverse = false;
		bool bStepBounded = true;
	};

	template <typename TPlanner>
	FDriverRun RunDirectDriver(
		const TArray<FFixedVector>& Route,
		FSeinMovementComponent Movement,
		FSeinNavigationComponent Navigation,
		const FFixedTransform& InitialPose,
		USeinNavigation* NavigationPolicy = nullptr)
	{
		FDriverRun Result;
		TPlanner* Planner = NewObject<TPlanner>();
		if (!Planner)
		{
			return Result;
		}
		FSeinEntity Entity;
		Entity.Transform = InitialPose;
		if (!PlanCoarseRoute(
				*Planner,
				Route,
				Entity,
				Movement,
				Navigation,
				NavigationPolicy,
				Result.PlannedPath))
		{
			return Result;
		}
		Result.PlannedPath.FlattenToWaypoints(
			FFixedPoint::FromInt(5));

		int32 Waypoint = 0;
		const FFixedPoint DeltaTime = FFixedPoint::One
			/ FFixedPoint::FromInt(DriverTickRate);
		const FFixedPoint AcceptanceSq =
			Navigation.AcceptanceRadius
			* Navigation.AcceptanceRadius;
		FSeinMovementContext Context{
			Entity,
			&Movement,
			&Navigation,
			Result.PlannedPath,
			Waypoint,
			AcceptanceSq,
			DeltaTime,
			NavigationPolicy,
			nullptr,
			FSeinEntityHandle(1, 1),
		};
		Planner->OnMoveBegin(Context);

		for (int32 Tick = 0; Tick < MaxDriverTicks; ++Tick)
		{
			const FFixedVector Before = Entity.Transform.GetLocation();
			const bool bCompleted = Planner->Tick(Context);
			FFixedVector Step = Entity.Transform.GetLocation() - Before;
			Step.Z = FFixedPoint::Zero;
			const FFixedPoint StepLimit = Movement.TopSpeed * DeltaTime
				+ FFixedPoint::FromInt(2);
			if (Step.Size() > StepLimit)
			{
				Result.bStepBounded = false;
			}
			const FFixedVector Forward =
				Entity.Transform.Rotation.RotateVector(
					FFixedVector::ForwardVector);
			Result.bSawReverse = Result.bSawReverse
				|| Movement.Velocity.X * Forward.X
					+ Movement.Velocity.Y * Forward.Y
					< FFixedPoint::Zero;
			FDriverFrame& Frame = Result.Frames.AddDefaulted_GetRef();
			Frame.Transform = Entity.Transform;
			Frame.Velocity = Movement.Velocity;
			Frame.Waypoint = Waypoint;
			Frame.bCompleted = bCompleted;
			if (bCompleted)
			{
				Result.bCompleted = true;
				Planner->OnMoveEnd(Entity);
				break;
			}
		}
		return Result;
	}

	bool DriverRunsEqual(const FDriverRun& A, const FDriverRun& B)
	{
		if (!PathsEqual(A.PlannedPath, B.PlannedPath)
			|| A.Frames.Num() != B.Frames.Num()
			|| A.bCompleted != B.bCompleted
			|| A.bSawReverse != B.bSawReverse
			|| A.bStepBounded != B.bStepBounded)
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Frames.Num(); ++Index)
		{
			if (!(A.Frames[Index] == B.Frames[Index]))
			{
				return false;
			}
		}
		return true;
	}

	struct FScopedVehicleGymRecipe
	{
		explicit FScopedVehicleGymRecipe(
			const FSeinVehicleGymNavigationRecipe& Recipe)
		{
			USeinVehicleGymNavigation::InstallRecipe(Recipe);
		}

		~FScopedVehicleGymRecipe()
		{
			USeinVehicleGymNavigation::ResetRecipe();
		}
	};

	struct FScopedVehicleGymSettings
	{
		explicit FScopedVehicleGymSettings(
			const FSeinVehicleGymNavigationRecipe& Recipe)
			: Settings(GetMutableDefault<USeinARTSCoreSettings>())
			, SavedNavigationClass(Settings->NavigationClass)
			, bSavedAsyncPathfinding(Settings->bAsyncPathfinding)
			, SavedPathBudget(Settings->PathRequestsPerTickBudget)
		{
			USeinVehicleGymNavigation::InstallRecipe(Recipe);
			Settings->NavigationClass = FSoftClassPath(
				USeinVehicleGymNavigation::StaticClass());
			Settings->bAsyncPathfinding = false;
			Settings->PathRequestsPerTickBudget = 1024;
			Settings->ApplySimPerformanceCvars();
		}

		~FScopedVehicleGymSettings()
		{
			Settings->NavigationClass = SavedNavigationClass;
			Settings->bAsyncPathfinding = bSavedAsyncPathfinding;
			Settings->PathRequestsPerTickBudget = SavedPathBudget;
			Settings->ApplySimPerformanceCvars();
			USeinVehicleGymNavigation::ResetRecipe();
		}

		USeinARTSCoreSettings* Settings = nullptr;
		FSoftClassPath SavedNavigationClass;
		bool bSavedAsyncPathfinding = false;
		int32 SavedPathBudget = 0;
	};

	struct FScopedFormationFacingSetting
	{
		FScopedFormationFacingSetting()
			: Settings(GetMutableDefault<USeinARTSCoreSettings>())
			, bSaved(Settings->bSettleToFormationFacing)
		{
			Settings->bSettleToFormationFacing = true;
		}

		~FScopedFormationFacingSetting()
		{
			Settings->bSettleToFormationFacing = bSaved;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		bool bSaved = false;
	};

	struct FScopedParallelSimulation
	{
		explicit FScopedParallelSimulation(bool bParallel)
		{
			IConsoleManager& Console = IConsoleManager::Get();
			Parallel = Console.FindConsoleVariable(
				TEXT("Sein.Sim.Parallel"));
			MinBatch = Console.FindConsoleVariable(
				TEXT("Sein.Sim.ParallelMinBatch"));
			if (!Parallel || !MinBatch)
			{
				return;
			}
			SavedParallel = Parallel->GetInt();
			SavedMinBatch = MinBatch->GetInt();
			Parallel->SetWithCurrentPriority(bParallel ? 1 : 0);
			MinBatch->SetWithCurrentPriority(1);
			bValid = Parallel->GetInt() == (bParallel ? 1 : 0)
				&& MinBatch->GetInt() == 1;
		}

		~FScopedParallelSimulation()
		{
			if (Parallel && MinBatch)
			{
				Parallel->SetWithCurrentPriority(SavedParallel);
				MinBatch->SetWithCurrentPriority(SavedMinBatch);
			}
		}

		bool IsValid() const { return bValid; }

		IConsoleVariable* Parallel = nullptr;
		IConsoleVariable* MinBatch = nullptr;
		int32 SavedParallel = 0;
		int32 SavedMinBatch = 0;
		bool bValid = false;
	};

	struct FVehicleWorld
	{
		USeinWorldSubsystem* World = nullptr;
		FSeinEntityHandle Entity;
		USeinAbility* Ability = nullptr;
		USeinMoveToProxy* Proxy = nullptr;
		USeinMoveToAction* Action = nullptr;

		bool IssueMove(
			const FFixedVector& Destination,
			FString& OutError)
		{
			if (!World || !Ability)
			{
				OutError = TEXT("Vehicle Gym move owner is not initialized");
				return false;
			}
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			Proxy = USeinMoveToProxy::SeinMoveTo(
				Ability, Destination);
			if (!Proxy)
			{
				OutError = TEXT("Vehicle Gym Move To proxy creation failed");
				return false;
			}
			Proxy->Activate();
			Action = UE::SeinARTSTests::
				FMoveToActionContinuationTestAccess::
				GetRunningAction(*Proxy);
			if (!Action)
			{
				OutError = TEXT("Vehicle Gym Move To activation failed");
				return false;
			}
			return true;
		}

		bool Initialize(
			FActorTestSpawner& Spawner,
			const FSeinMovementComponent& Movement,
			const FSeinNavigationComponent& Navigation,
			const FFixedTransform& InitialPose,
			const FFixedVector& Destination,
			FName FixtureId,
			FString& OutError,
			TFunction<void(
				USeinWorldSubsystem&,
				FSeinEntityHandle)> AddFixtureState = {})
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				OutError = TEXT("world subsystem is missing");
				return false;
			}
			int32 AbilityId = INDEX_NONE;
			if (!SeinTestMatchBootstrap::Materialize(
					*World,
					[&]()
					{
						Entity = World->SpawnAbstractEntity(
							InitialPose,
							FSeinPlayerID::Neutral());
						World->AddComponent(Entity, Movement);
						World->AddComponent(Entity, Navigation);
						World->AddComponent(
							Entity, FSeinAbilityComponent());
						if (AddFixtureState)
						{
							AddFixtureState(*World, Entity);
						}
						AbilityId = USeinAbilityBPFL::SeinGrantAbility(
							World,
							Entity,
							USeinVehicleGymAbility::StaticClass());
					},
					FSeinMatchSettings(),
					0x5647594D,
					FixtureId,
					&OutError)
				|| !SeinTestMatchBootstrap::Start(*World, &OutError))
			{
				return false;
			}

			Ability = World->GetAbilityInstance(AbilityId);
			if (!Ability || !World->LatentActionManager)
			{
				OutError = TEXT("Vehicle Gym ability or latent manager is missing");
				return false;
			}
			{
				auto Scope = FSeinSimContextTestAccess::Enter(*World);
				if (!Ability->ActivateAbility(
						FSeinEntityHandle::Invalid(),
						Destination))
				{
					OutError = TEXT("Vehicle Gym ability activation failed");
					return false;
				}
			}
			return IssueMove(Destination, OutError);
		}
	};

	void TickRunningWorlds(USeinWorldSubsystem& Reference, int32 TickCount)
	{
		for (int32 Tick = 0; Tick < TickCount; ++Tick)
		{
			FTSTicker::GetCoreTicker().Tick(
				Reference.GetFixedDeltaTimeSeconds());
		}
	}

	bool ComputeRoot(USeinWorldSubsystem& World, FGuid& OutRoot)
	{
		FString Error;
		return World.ComputeCanonicalStateRoot(OutRoot, Error);
	}

	struct FVehicleRootTrace
	{
		TArray<FGuid> Roots;
		bool bParallelModeObserved = false;
		bool bValid = false;
	};

	FVehicleRootTrace RunVehicleRootTrace(
		bool bParallel,
		const FSeinMovementComponent& Movement,
		const FSeinNavigationComponent& Navigation,
		const FFixedVector& Destination,
		int32 TickCount)
	{
		FVehicleRootTrace Trace;
		FScopedParallelSimulation ParallelScope(bParallel);
		Trace.bParallelModeObserved = ParallelScope.IsValid();
		if (!Trace.bParallelModeObserved)
		{
			return Trace;
		}

		FActorTestSpawner Spawner;
		FVehicleWorld Vehicle;
		FString Error;
		if (!Vehicle.Initialize(
				Spawner,
				Movement,
				Navigation,
				Pose(Point(0)),
				Destination,
				TEXT("VehicleGym.SerialParallelRecovery"),
				Error))
		{
			return Trace;
		}

		Trace.Roots.Reserve(TickCount);
		for (int32 Tick = 0; Tick < TickCount; ++Tick)
		{
			TickRunningWorlds(*Vehicle.World, 1);
			FGuid Root;
			if (!ComputeRoot(*Vehicle.World, Root))
			{
				Vehicle.World->StopSimulation();
				return Trace;
			}
			Trace.Roots.Add(Root);
		}
		Vehicle.World->StopSimulation();
		Trace.bValid = Trace.Roots.Num() == TickCount;
		return Trace;
	}

	bool IsEntityMovingInReverse(
		const USeinWorldSubsystem& World,
		FSeinEntityHandle EntityHandle)
	{
		const FSeinEntity* Entity = World.GetEntity(EntityHandle);
		const FSeinMovementComponent* Movement =
			World.GetComponent<FSeinMovementComponent>(EntityHandle);
		if (!Entity || !Movement)
		{
			return false;
		}
		const FFixedVector Forward =
			Entity->Transform.Rotation.RotateVector(
				FFixedVector::ForwardVector);
		return Movement->Velocity.X * Forward.X
			+ Movement->Velocity.Y * Forward.Y
			< FFixedPoint::Zero;
	}

	bool RunSnapshotContinuation(
		const FSeinVehicleGymNavigationRecipe& Recipe,
		const FSeinMovementComponent& Movement,
		const FSeinNavigationComponent& Navigation,
		const FFixedVector& Destination,
		int32 PreCaptureTicks,
		bool bRequireArc,
		bool bRequireReverse,
		FString& OutError,
		bool bRequireReverseMotion = false,
		bool bRequireCompletion = true,
		int32 FutureTickLimit = MaxDriverTicks,
		TFunction<bool(
			USeinWorldSubsystem&,
			FSeinEntityHandle,
			FString&)> PrepareCapture = {})
	{
		FScopedVehicleGymSettings Settings(Recipe);
		FActorTestSpawner SourceSpawner;
		FVehicleWorld Source;
		if (!Source.Initialize(
				SourceSpawner,
				Movement,
				Navigation,
				Pose(Point(0)),
				Destination,
				FName(*FString::Printf(
					TEXT("VehicleGym.%s.Source"),
					*Recipe.ScenarioId.ToString())),
				OutError))
		{
			return false;
		}

		TickRunningWorlds(*Source.World, PreCaptureTicks);
		if (PrepareCapture
			&& !PrepareCapture(
				*Source.World, Source.Entity, OutError))
		{
			return false;
		}
		if (!Source.Action || Source.Action->bCompleted
			|| !Source.Action->Path.bIsValid
			|| (bRequireArc && !PathHasArc(Source.Action->Path))
			|| (bRequireReverse && !PathHasReverse(Source.Action->Path)))
		{
			OutError = TEXT("source did not reach the required active maneuver state");
			return false;
		}
		const FSeinEntity* MidEntity = Source.World->GetEntity(Source.Entity);
		if (!MidEntity || MidEntity->Transform.GetLocation() == Point(0))
		{
			OutError = TEXT("source did not move before checkpoint capture");
			return false;
		}
		if (bRequireReverseMotion
			&& !IsEntityMovingInReverse(*Source.World, Source.Entity))
		{
			OutError = TEXT("source was not inside reverse recovery at checkpoint capture");
			return false;
		}

		Source.World->StopSimulation();
		FSeinWorldSnapshot Snapshot;
		Source.World->CaptureSnapshot(Snapshot);
		if (Snapshot.SnapshotVersion != FSeinWorldSnapshot::CurrentVersion)
		{
			OutError = TEXT("checkpoint capture failed");
			return false;
		}

		FActorTestSpawner DestinationSpawner;
		USeinWorldSubsystem* DestinationWorld =
			DestinationSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		if (!DestinationWorld
			|| !SeinTestSnapshotRestore::RestoreTrusted(
				*DestinationWorld, Snapshot, &OutError)
			|| DestinationWorld->LatentActionManager
				->GetActiveActionCount() != 1)
		{
			return false;
		}
		USeinMoveToAction* DestinationAction = Cast<USeinMoveToAction>(
			DestinationWorld->LatentActionManager->GetActiveActions()[0]);
		if (!DestinationAction
			|| !PathsEqual(Source.Action->Path, DestinationAction->Path))
		{
			OutError = TEXT("restored move continuation lost its typed path");
			return false;
		}
		if (!Source.World->StartSimulation()
			|| !DestinationWorld->StartSimulation())
		{
			OutError = TEXT("restored timelines could not restart");
			return false;
		}

		FGuid SourceRoot;
		FGuid DestinationRoot;
		if (!ComputeRoot(*Source.World, SourceRoot)
			|| !ComputeRoot(*DestinationWorld, DestinationRoot)
			|| SourceRoot != DestinationRoot)
		{
			OutError = TEXT("restored root differs at the checkpoint boundary");
			return false;
		}

		bool bBothCompleted = false;
		for (int32 Step = 0; Step < FutureTickLimit; ++Step)
		{
			TickRunningWorlds(*Source.World, 1);
			if (Source.World->GetCurrentTick()
				!= DestinationWorld->GetCurrentTick()
				|| !ComputeRoot(*Source.World, SourceRoot)
				|| !ComputeRoot(*DestinationWorld, DestinationRoot)
				|| SourceRoot != DestinationRoot)
			{
				OutError = FString::Printf(
					TEXT("restored timelines diverged at future step %d"),
					Step);
				return false;
			}
			const int32 SourceActions =
				Source.World->LatentActionManager->GetActiveActionCount();
			const int32 DestinationActions =
				DestinationWorld->LatentActionManager->GetActiveActionCount();
			if (SourceActions != DestinationActions)
			{
				OutError = TEXT("restored action lifecycle diverged");
				return false;
			}
			if (SourceActions == 0)
			{
				bBothCompleted = true;
				break;
			}
		}
		Source.World->StopSimulation();
		DestinationWorld->StopSimulation();
		if (!bRequireCompletion)
		{
			return true;
		}
		if (!bBothCompleted)
		{
			OutError = TEXT("restored maneuver did not finish inside the gym bound");
		}
		return bBothCompleted;
	}
}

TEST(VehicleGymPlannerCoversProductionArchetypeContracts,
	"SeinARTS.Integration.MovementPlus.VehicleGym.Planner")
{
	FScopedVehicleGymRecipe ScopedRecipe{
		FSeinVehicleGymNavigationRecipe()};
	USeinVehicleGymNavigation* NavigationPolicy =
		NewObject<USeinVehicleGymNavigation>();
	ASSERT_THAT(IsNotNull(NavigationPolicy));

	FString Error;
	FSeinEntity Entity;
	Entity.Transform = Pose(Point(0));

	// Wheeled scout: an open behind-goal must become a forward arc, while a
	// short behind-goal must become an exact reverse word.
	FSeinNavigationComponent ScoutNavigation;
	FSeinMovementComponent Scout = MakeWheeledComponent(
		900, 3, 2, 240, 85, ScoutNavigation);
	USeinVehicleGymWheeledPlanner* ScoutPlanner =
		NewObject<USeinVehicleGymWheeledPlanner>();
	ASSERT_THAT(IsNotNull(ScoutPlanner));
	const TArray<FFixedVector> ScoutUTurn = {
		Point(-1200), Point(-4000) };
	FSeinPath ScoutPath;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*ScoutPlanner,
		ScoutUTurn,
		Entity,
		Scout,
		ScoutNavigation,
		NavigationPolicy,
		ScoutPath)));
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		ScoutPath, ScoutUTurn.Last(), Error)));
	ASSERT_THAT(IsTrue(PathHasArc(ScoutPath)));
	ASSERT_THAT(IsFalse(PathHasReverse(ScoutPath)));
	ASSERT_THAT(IsTrue(
		USeinVehicleGymNavigation::GetOccupancyQueryCount() <= 4096));

	USeinVehicleGymNavigation::ResetOccupancyQueryCount();
	USeinVehicleGymWheeledPlanner* RepeatScoutPlanner =
		NewObject<USeinVehicleGymWheeledPlanner>();
	FSeinPath RepeatScoutPath;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*RepeatScoutPlanner,
		ScoutUTurn,
		Entity,
		Scout,
		ScoutNavigation,
		NavigationPolicy,
		RepeatScoutPath)));
	ASSERT_THAT(IsTrue(PathsEqual(ScoutPath, RepeatScoutPath)));

	const TArray<FFixedVector> ScoutReverse = { Point(-400) };
	FSeinPath ScoutReversePath;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*ScoutPlanner,
		ScoutReverse,
		Entity,
		Scout,
		ScoutNavigation,
		NavigationPolicy,
		ScoutReversePath)));
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		ScoutReversePath, ScoutReverse.Last(), Error)));
	ASSERT_THAT(IsTrue(PathHasReverse(ScoutReversePath)));

	// Logistics truck: its larger wheelbase still produces a valid bounded
	// open-ground maneuver without relocating the command destination.
	FSeinNavigationComponent TruckNavigation;
	FSeinMovementComponent Truck = MakeWheeledComponent(
		500, 13, 20, 450, 140, TruckNavigation);
	USeinVehicleGymWheeledPlanner* TruckPlanner =
		NewObject<USeinVehicleGymWheeledPlanner>();
	const TArray<FFixedVector> TruckRoute = {
		Point(-1600), Point(-5200) };
	FSeinPath TruckPath;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*TruckPlanner,
		TruckRoute,
		Entity,
		Truck,
		TruckNavigation,
		NavigationPolicy,
		TruckPath)));
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		TruckPath, TruckRoute.Last(), Error)));
	ASSERT_THAT(IsTrue(PathHasArc(TruckPath)));

	// Pivot-capable MBT: stationary turnaround remains a straight coarse path
	// for its runtime pivot policy; the same order at speed becomes a momentum
	// arc. This is an intentional mode distinction, not missing planning.
	FSeinNavigationComponent MbtNavigation;
	FSeinMovementComponent Mbt = MakeTrackedComponent(
		550, 1, 1, 0, 160, MbtNavigation);
	USeinVehicleGymTrackedPlanner* MbtPlanner =
		NewObject<USeinVehicleGymTrackedPlanner>();
	const TArray<FFixedVector> MbtRoute = {
		Point(-1000), Point(-3600) };
	FSeinPath MbtStationaryPath;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*MbtPlanner,
		MbtRoute,
		Entity,
		Mbt,
		MbtNavigation,
		NavigationPolicy,
		MbtStationaryPath)));
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		MbtStationaryPath, MbtRoute.Last(), Error)));
	ASSERT_THAT(IsFalse(PathHasArc(MbtStationaryPath)));
	ASSERT_THAT(IsFalse(PathHasReverse(MbtStationaryPath)));

	Mbt.Velocity = Point(400);
	FSeinPath MbtMomentumPath;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*MbtPlanner,
		MbtRoute,
		Entity,
		Mbt,
		MbtNavigation,
		NavigationPolicy,
		MbtMomentumPath)));
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		MbtMomentumPath, MbtRoute.Last(), Error)));
	ASSERT_THAT(IsTrue(PathHasArc(MbtMomentumPath)));

	// Non-neutral-steer IFV/APC: authored turn radius opts into the full
	// shared maneuver ladder even from rest.
	FSeinNavigationComponent IfvNavigation;
	FSeinMovementComponent Ifv = MakeTrackedComponent(
		650, 6, 5, 450, 130, IfvNavigation);
	USeinVehicleGymTrackedPlanner* IfvPlanner =
		NewObject<USeinVehicleGymTrackedPlanner>();
	FSeinPath IfvPath;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*IfvPlanner,
		MbtRoute,
		Entity,
		Ifv,
		IfvNavigation,
		NavigationPolicy,
		IfvPath)));
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		IfvPath, MbtRoute.Last(), Error)));
	ASSERT_THAT(IsTrue(PathHasArc(IfvPath)));

}

TEST(VehicleGymCorridorProducesBoundedCuspedEscape,
	"SeinARTS.Integration.MovementPlus.VehicleGym.Corridor")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("CorridorEscape");
	Recipe.bUseCorridor = true;
	Recipe.CorridorOpenAtOrBelowX = FFixedPoint::FromInt(-900);
	Recipe.CorridorHalfWidth = FFixedPoint::FromInt(250);
	FScopedVehicleGymRecipe ScopedRecipe(Recipe);
	USeinVehicleGymNavigation* NavigationPolicy =
		NewObject<USeinVehicleGymNavigation>();
	ASSERT_THAT(IsNotNull(NavigationPolicy));

	FSeinNavigationComponent Navigation;
	FSeinMovementComponent Movement = MakeWheeledComponent(
		500, 1, 1, 350, 50, Navigation);
	FSeinWheeledMovementData* Wheeled =
		Movement.MovementClassData.GetMutablePtr<FSeinWheeledMovementData>();
	ASSERT_THAT(IsNotNull(Wheeled));
	// The pocket deliberately sits beyond the shipping default 1,200-unit
	// reverse-out search. This archetype opts into a longer heavy-vehicle
	// search so the scenario exercises the bounded corridor-escape word.
	Wheeled->ReversePlanMaxDistance = FFixedPoint::FromInt(5000);
	FSeinEntity Entity;
	Entity.Transform = Pose(Point(0));
	const TArray<FFixedVector> Route = {
		Point(-300),
		Point(-650),
		Point(-1000),
		// Leave one full two-radius pocket plus the footprint ring behind
		// the corridor mouth before asking the planner to turn around.
		Point(-2400),
		Point(-4000),
	};
	USeinVehicleGymWheeledPlanner* Planner =
		NewObject<USeinVehicleGymWheeledPlanner>();
	FSeinPath Path;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*Planner,
		Route,
		Entity,
		Movement,
		Navigation,
		NavigationPolicy,
		Path)));
	FString Error;
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		Path, Route.Last(), Error)));
	ASSERT_THAT(IsTrue(PathHasReverse(Path)));
	ASSERT_THAT(IsTrue(PathHasCusp(Path)));
	ASSERT_THAT(IsTrue(
		USeinVehicleGymNavigation::GetOccupancyQueryCount() <= 4096));

	FSeinPath Flattened = Path;
	Flattened.FlattenToWaypoints(FFixedPoint::FromInt(5));
	for (const FFixedVector& Waypoint : Flattened.Waypoints)
	{
		ASSERT_THAT(IsTrue(NavigationPolicy->IsPassable(Waypoint)));
	}
}

TEST(VehicleGymConfinedTurnUsesExplicitKTurn,
	"SeinARTS.Integration.MovementPlus.VehicleGym.KTurn")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("ConfinedKTurn");
	Recipe.bUseCorridor = true;
	Recipe.CorridorOpenAtOrBelowX = FFixedPoint::FromInt(-10000);
	Recipe.CorridorHalfWidth = FFixedPoint::FromInt(400);
	FScopedVehicleGymRecipe ScopedRecipe(Recipe);
	USeinVehicleGymNavigation* NavigationPolicy =
		NewObject<USeinVehicleGymNavigation>();
	ASSERT_THAT(IsNotNull(NavigationPolicy));

	FSeinNavigationComponent Navigation;
	FSeinMovementComponent Movement = MakeWheeledComponent(
		500, 1, 1, 300, 50, Navigation);
	FSeinWheeledMovementData* Wheeled =
		Movement.MovementClassData.GetMutablePtr<FSeinWheeledMovementData>();
	ASSERT_THAT(IsNotNull(Wheeled));
	// Disable the later reverse-out candidate so this fixture proves the
	// alternating swing word itself, rather than accepting another cusp word.
	Wheeled->ReversePlanMaxDistance = FFixedPoint::One;

	FSeinEntity Entity;
	Entity.Transform = Pose(Point(0));
	const TArray<FFixedVector> Route = {
		Point(-500),
		Point(-1200),
		Point(-2500),
	};
	USeinVehicleGymWheeledPlanner* Planner =
		NewObject<USeinVehicleGymWheeledPlanner>();
	FSeinPath Path;
	ASSERT_THAT(IsTrue(PlanCoarseRoute(
		*Planner,
		Route,
		Entity,
		Movement,
		Navigation,
		NavigationPolicy,
		Path)));
	FString Error;
	ASSERT_THAT(IsTrue(ValidateTypedPath(
		Path, Route.Last(), Error)));
	ASSERT_THAT(IsTrue(PathHasArc(Path)));
	ASSERT_THAT(IsTrue(PathHasReverse(Path)));
	ASSERT_THAT(IsTrue(PathHasCusp(Path)));
	ASSERT_THAT(IsTrue(
		USeinVehicleGymNavigation::GetOccupancyQueryCount() <= 4096));
}

TEST(VehicleGymIntervalRepathAndStopReissueComplete,
	"SeinARTS.Integration.MovementPlus.VehicleGym.OrderLifecycle")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("IntervalRepathAndReissue");
	Recipe.Route = { Point(1000), Point(4000) };
	Recipe.RepathRoute = {
		Point(900, 700),
		Point(2500, 700),
		Point(4000),
	};
	Recipe.RepathRouteStartX = FFixedPoint::Zero;
	Recipe.bTrimRepathRouteByStartX = true;
	FScopedVehicleGymSettings Settings(Recipe);

	FSeinNavigationComponent Navigation;
	FSeinMovementComponent Movement = MakeWheeledComponent(
		700, 1, 1, 300, 90, Navigation);
	Navigation.RepathMode = ESeinRepathMode::Interval;
	Navigation.RepathInterval = FFixedPoint::One
		/ FFixedPoint::FromInt(10);

	FActorTestSpawner Spawner;
	FVehicleWorld Vehicle;
	FString Error;
	ASSERT_THAT(IsTrue(Vehicle.Initialize(
		Spawner,
		Movement,
		Navigation,
		Pose(Point(0)),
		Recipe.Route.Last(),
		TEXT("VehicleGym.IntervalRepathAndReissue"),
		Error)));

	TickRunningWorlds(*Vehicle.World, 12);
	ASSERT_THAT(IsTrue(Vehicle.Action != nullptr
		&& !Vehicle.Action->bCompleted));
	ASSERT_THAT(IsTrue(
		USeinVehicleGymNavigation::GetPathQueryCount() >= 2));
	ASSERT_THAT(IsTrue(PathHasLateralWaypoint(Vehicle.Action->Path)));

	USeinMoveToAction* CancelledAction = Vehicle.Action;
	{
		auto Scope = FSeinSimContextTestAccess::Enter(*Vehicle.World);
		Vehicle.World->LatentActionManager->
			CancelActionsForEntityOfClass(
				Vehicle.Entity,
				USeinMoveToAction::StaticClass());
		Vehicle.World->LatentActionManager->CleanupCompleted();
	}
	ASSERT_THAT(IsTrue(CancelledAction->bCancelled));
	ASSERT_THAT(AreEqual(
		0,
		Vehicle.World->LatentActionManager->GetActiveActionCount()));

	const FFixedVector ReissuedDestination = Point(5200);
	ASSERT_THAT(IsTrue(Vehicle.IssueMove(
		ReissuedDestination, Error)));
	bool bCompleted = false;
	for (int32 Tick = 0; Tick < MaxDriverTicks; ++Tick)
	{
		TickRunningWorlds(*Vehicle.World, 1);
		if (Vehicle.World->LatentActionManager
			->GetActiveActionCount() == 0)
		{
			bCompleted = true;
			break;
		}
	}
	Vehicle.World->StopSimulation();
	ASSERT_THAT(IsTrue(bCompleted));
	const FSeinEntity* Settled = Vehicle.World->GetEntity(Vehicle.Entity);
	ASSERT_THAT(IsNotNull(Settled));
	FFixedVector FinalDelta = Settled->Transform.GetLocation()
		- ReissuedDestination;
	FinalDelta.Z = FFixedPoint::Zero;
	ASSERT_THAT(IsTrue(FinalDelta.SizeSquared()
		<= Navigation.AcceptanceRadius * Navigation.AcceptanceRadius));
}

TEST(VehicleGymSettlesToFormationSlotFacing,
	"SeinARTS.Integration.MovementPlus.VehicleGym.FormationFacing")
{
	FScopedFormationFacingSetting FacingSetting;
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("FormationFacing");
	const FFixedVector Destination = Point(1200);
	Recipe.Route = { Destination };
	FScopedVehicleGymSettings Settings(Recipe);

	FSeinNavigationComponent Navigation;
	FSeinMovementComponent Movement = MakeWheeledComponent(
		500, 1, 1, 300, 70, Navigation);
	FSeinEntityHandle Broker;
	FActorTestSpawner Spawner;
	FVehicleWorld Vehicle;
	FString Error;
	ASSERT_THAT(IsTrue(Vehicle.Initialize(
		Spawner,
		Movement,
		Navigation,
		Pose(Point(0)),
		Destination,
		TEXT("VehicleGym.FormationFacing"),
		Error,
		[&](USeinWorldSubsystem& World, FSeinEntityHandle Entity)
		{
			Broker = World.SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			FSeinCommandBrokerData BrokerData;
			BrokerData.Members.Add(Entity);
			BrokerData.bSelfCullOnEmpty = false;
			BrokerData.SettledSlotPositions.Add(Destination);
			BrokerData.SettledSlotFacings.Add(
				FFixedQuaternion::MakeFromEulers(FFixedVector(
					FFixedPoint::Zero,
					FFixedPoint::Zero,
					FFixedPoint::Pi * FFixedPoint::Half)));
			World.AddComponent(Broker, BrokerData);
			FSeinBrokerMembershipData Membership;
			Membership.CurrentBrokerHandle = Broker;
			World.AddComponent(Entity, Membership);
		})));
	ASSERT_THAT(IsTrue(Broker.IsValid()));

	bool bArrived = false;
	for (int32 Tick = 0; Tick < MaxDriverTicks; ++Tick)
	{
		TickRunningWorlds(*Vehicle.World, 1);
		if (Vehicle.World->LatentActionManager
			->GetActiveActionCount() == 0)
		{
			bArrived = true;
			break;
		}
	}
	ASSERT_THAT(IsTrue(bArrived));
	TickRunningWorlds(*Vehicle.World, DriverTickRate * 3);
	Vehicle.World->StopSimulation();
	const FSeinEntity* Settled = Vehicle.World->GetEntity(Vehicle.Entity);
	ASSERT_THAT(IsNotNull(Settled));
	const FFixedVector Forward =
		Settled->Transform.Rotation.RotateVector(
			FFixedVector::ForwardVector);
	ASSERT_THAT(IsTrue(Forward.Y > FFixedPoint::FromInt(99)
		/ FFixedPoint::FromInt(100)));
	const FFixedPoint AbsX = Forward.X < FFixedPoint::Zero
		? -Forward.X
		: Forward.X;
	ASSERT_THAT(IsTrue(AbsX < FFixedPoint::One
		/ FFixedPoint::FromInt(100)));
}

TEST(VehicleGymDriversCompleteAndRepeatExactly,
	"SeinARTS.Determinism.MovementPlus.VehicleGym.Driver")
{
	FScopedVehicleGymRecipe ScopedRecipe{
		FSeinVehicleGymNavigationRecipe()};
	FSeinNavigationComponent ScoutNavigation;
	FSeinMovementComponent Scout = MakeWheeledComponent(
		900, 3, 2, 240, 85, ScoutNavigation);
	const TArray<FFixedVector> UTurn = {
		Point(-1200), Point(-4000) };
	const FDriverRun UTurnA =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			UTurn, Scout, ScoutNavigation, Pose(Point(0)));
	const FDriverRun UTurnB =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			UTurn, Scout, ScoutNavigation, Pose(Point(0)));
	ASSERT_THAT(IsTrue(UTurnA.bCompleted));
	ASSERT_THAT(IsTrue(UTurnA.bStepBounded));
	ASSERT_THAT(IsTrue(PathHasArc(UTurnA.PlannedPath)));
	ASSERT_THAT(IsTrue(DriverRunsEqual(UTurnA, UTurnB)));

	const TArray<FFixedVector> Reverse = { Point(-400) };
	const FDriverRun ReverseA =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			Reverse, Scout, ScoutNavigation, Pose(Point(0)));
	const FDriverRun ReverseB =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			Reverse, Scout, ScoutNavigation, Pose(Point(0)));
	ASSERT_THAT(IsTrue(ReverseA.bCompleted));
	ASSERT_THAT(IsTrue(ReverseA.bSawReverse));
	ASSERT_THAT(IsTrue(ReverseA.bStepBounded));
	ASSERT_THAT(IsTrue(DriverRunsEqual(ReverseA, ReverseB)));

	const TArray<FFixedVector> SBend = {
		Point(1000),
		Point(1800, 600),
		Point(2600, -600),
		Point(3400),
	};
	const FDriverRun SBendA =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			SBend, Scout, ScoutNavigation, Pose(Point(0)));
	const FDriverRun SBendB =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			SBend, Scout, ScoutNavigation, Pose(Point(0)));
	ASSERT_THAT(IsTrue(SBendA.bCompleted));
	ASSERT_THAT(IsTrue(SBendA.bStepBounded));
	ASSERT_THAT(IsTrue(DriverRunsEqual(SBendA, SBendB)));

	FSeinNavigationComponent MbtNavigation;
	FSeinMovementComponent Mbt = MakeTrackedComponent(
		550, 1, 1, 0, 160, MbtNavigation);
	const FDriverRun MbtA =
		RunDirectDriver<USeinVehicleGymTrackedPlanner>(
			UTurn, Mbt, MbtNavigation, Pose(Point(0)));
	const FDriverRun MbtB =
		RunDirectDriver<USeinVehicleGymTrackedPlanner>(
			UTurn, Mbt, MbtNavigation, Pose(Point(0)));
	ASSERT_THAT(IsTrue(MbtA.bCompleted));
	ASSERT_THAT(IsTrue(MbtA.bStepBounded));
	ASSERT_THAT(IsTrue(DriverRunsEqual(MbtA, MbtB)));
}

TEST(VehicleGymRecoveryIsDeterministicAndBounded,
	"SeinARTS.Determinism.MovementPlus.VehicleGym.Recovery")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("BlockedRecovery");
	Recipe.Route = { Point(2000) };
	FSeinVehicleGymBlockedRect& Wall =
		Recipe.BlockedRects.AddDefaulted_GetRef();
	Wall.MinX = FFixedPoint::FromInt(300);
	Wall.MaxX = FFixedPoint::FromInt(700);
	Wall.MinY = FFixedPoint::FromInt(-1000);
	Wall.MaxY = FFixedPoint::FromInt(1000);
	FScopedVehicleGymRecipe ScopedRecipe(Recipe);
	USeinVehicleGymNavigation* NavigationPolicy =
		NewObject<USeinVehicleGymNavigation>();
	ASSERT_THAT(IsNotNull(NavigationPolicy));

	FSeinNavigationComponent Navigation;
	FSeinMovementComponent Movement = MakeWheeledComponent(
		500, 1, 1, 300, 50, Navigation);
	const FDriverRun RecoveryA =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			Recipe.Route,
			Movement,
			Navigation,
			Pose(Point(0)),
			NavigationPolicy);
	const FDriverRun RecoveryB =
		RunDirectDriver<USeinVehicleGymWheeledPlanner>(
			Recipe.Route,
			Movement,
			Navigation,
			Pose(Point(0)),
			NavigationPolicy);
	ASSERT_THAT(IsFalse(RecoveryA.bCompleted));
	ASSERT_THAT(IsTrue(RecoveryA.bSawReverse));
	ASSERT_THAT(IsTrue(RecoveryA.bStepBounded));
	ASSERT_THAT(IsTrue(DriverRunsEqual(RecoveryA, RecoveryB)));
}

TEST(VehicleGymRecoveryMatchesSerialAndParallelRoots,
	"SeinARTS.Determinism.MovementPlus.VehicleGym.SerialParallelRecovery")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("SerialParallelRecovery");
	Recipe.Route = { Point(2000) };
	FSeinVehicleGymBlockedRect& Wall =
		Recipe.BlockedRects.AddDefaulted_GetRef();
	Wall.MinX = FFixedPoint::FromInt(300);
	Wall.MaxX = FFixedPoint::FromInt(700);
	Wall.MinY = FFixedPoint::FromInt(-1000);
	Wall.MaxY = FFixedPoint::FromInt(1000);
	FScopedVehicleGymSettings Settings(Recipe);

	FSeinNavigationComponent Navigation;
	const FSeinMovementComponent Movement = MakeWheeledComponent(
		500, 1, 1, 300, 50, Navigation);
	constexpr int32 TraceTicks = DriverTickRate * 6;
	const FVehicleRootTrace Serial = RunVehicleRootTrace(
		false, Movement, Navigation, Recipe.Route.Last(), TraceTicks);
	const FVehicleRootTrace Parallel = RunVehicleRootTrace(
		true, Movement, Navigation, Recipe.Route.Last(), TraceTicks);
	ASSERT_THAT(IsTrue(Serial.bParallelModeObserved));
	ASSERT_THAT(IsTrue(Parallel.bParallelModeObserved));
	ASSERT_THAT(IsTrue(Serial.bValid));
	ASSERT_THAT(IsTrue(Parallel.bValid));
	ASSERT_THAT(AreEqual(Serial.Roots.Num(), Parallel.Roots.Num()));
	for (int32 Tick = 0; Tick < Serial.Roots.Num(); ++Tick)
	{
		ASSERT_THAT(AreEqual(Serial.Roots[Tick], Parallel.Roots[Tick]));
	}
}

TEST(VehicleGymActiveArcAndReverseSnapshotsContinueExactly,
	"SeinARTS.Determinism.MovementPlus.VehicleGym.Snapshot")
{
	FString Error;

	FSeinNavigationComponent ScoutNavigation;
	FSeinMovementComponent Scout = MakeWheeledComponent(
		900, 3, 2, 240, 85, ScoutNavigation);
	FSeinVehicleGymNavigationRecipe ArcRecipe;
	ArcRecipe.ScenarioId = TEXT("ArcSnapshot");
	ArcRecipe.Route = { Point(-1200), Point(-4000) };
	ASSERT_THAT(IsTrue(RunSnapshotContinuation(
		ArcRecipe,
		Scout,
		ScoutNavigation,
		ArcRecipe.Route.Last(),
		20,
		true,
		false,
		Error)));

	FSeinVehicleGymNavigationRecipe ReverseRecipe;
	ReverseRecipe.ScenarioId = TEXT("ReverseSnapshot");
	ReverseRecipe.Route = { Point(-400) };
	ASSERT_THAT(IsTrue(RunSnapshotContinuation(
		ReverseRecipe,
		Scout,
		ScoutNavigation,
		ReverseRecipe.Route.Last(),
		4,
		false,
		true,
		Error)));

	FSeinVehicleGymNavigationRecipe RecoveryRecipe;
	RecoveryRecipe.ScenarioId = TEXT("RecoverySnapshot");
	RecoveryRecipe.Route = { Point(2000) };
	FSeinVehicleGymBlockedRect& RecoveryWall =
		RecoveryRecipe.BlockedRects.AddDefaulted_GetRef();
	RecoveryWall.MinX = FFixedPoint::FromInt(300);
	RecoveryWall.MaxX = FFixedPoint::FromInt(700);
	RecoveryWall.MinY = FFixedPoint::FromInt(-1000);
	RecoveryWall.MaxY = FFixedPoint::FromInt(1000);
	FSeinNavigationComponent RecoveryNavigation;
	FSeinMovementComponent RecoveryMovement = MakeWheeledComponent(
		500, 1, 1, 300, 50, RecoveryNavigation);
	ASSERT_THAT(IsTrue(RunSnapshotContinuation(
		RecoveryRecipe,
		RecoveryMovement,
		RecoveryNavigation,
		RecoveryRecipe.Route.Last(),
		10,
		false,
		false,
		Error,
		true,
		false,
		60,
		[](USeinWorldSubsystem& World,
			FSeinEntityHandle Entity,
			FString& OutError)
		{
			USeinMovementSubsystem* MovementSubsystem =
				World.GetWorld()
					? World.GetWorld()->GetSubsystem<
						USeinMovementSubsystem>()
					: nullptr;
			UObject* Instance = MovementSubsystem
				? MovementSubsystem->FindMovementInstance(Entity)
				: nullptr;
			auto Scope = FSeinSimContextTestAccess::Enter(World);
			FSeinMovementComponent* Movement =
				World.GetComponentMutable<FSeinMovementComponent>(Entity);
			if (!Instance || !Movement
				|| !SetFixedProperty(
					*Instance,
					TEXT("RecoveryTime"),
					FFixedPoint::FromInt(3)
						/ FFixedPoint::FromInt(5))
				|| !SetIntProperty(
					*Instance, TEXT("RecoveryDir"), -1))
			{
				OutError = TEXT("could not seed the active recovery checkpoint state");
				return false;
			}
			Movement->Velocity = Point(-100);
			return true;
		})));
}

TEST(VehicleGymMixedTrafficCheckpointContinuesExactly,
	"SeinARTS.Determinism.MovementPlus.VehicleGym.MixedTraffic")
{
	FSeinVehicleGymNavigationRecipe Recipe;
	Recipe.ScenarioId = TEXT("MixedTraffic");
	FScopedVehicleGymSettings Settings(Recipe);
	FActorTestSpawner SourceSpawner;
	USeinWorldSubsystem* Source =
		SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Source));

	const FFixedVector Starts[] = {
		Point(-1000, -150),
		Point(-1300, 150),
		Point(900, -150),
		Point(1200, 150),
	};
	const FFixedVector Destinations[] = {
		Point(1500, -150),
		Point(1500, 150),
		Point(-1500, -150),
		Point(-1500, 150),
	};
	TArray<FSeinEntityHandle> Entities;
	TArray<int32> AbilityIds;
	FString Error;
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
		*Source,
		[&]()
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const bool bVehicle = Index < 2;
				FSeinNavigationComponent Navigation;
				FSeinMovementComponent Movement;
				if (bVehicle)
				{
					Movement = MakeWheeledComponent(
						600, 1, 1, 300, 100, Navigation);
				}
				else
				{
					Movement.MovementClass = FSoftClassPath(
						USeinInfantryMovement::StaticClass());
					Movement.TopSpeed = FFixedPoint::FromInt(450);
					Movement.TurnRate = FFixedPoint::Pi;
					Navigation.FallbackFootprintRadius =
						FFixedPoint::FromInt(45);
					Navigation.AcceptanceRadius =
						FFixedPoint::FromInt(60);
					Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
				}

				FSeinExtentsComponent Extents;
				Extents.bCollisionEnabled = true;
				Extents.Mobility = ESeinCollisionMobility::Movable;
				Extents.Mass = FFixedPoint::FromInt(
					bVehicle ? 1000 : 50);
				Extents.ObjectType.Channel = TEXT("Default");
				FSeinExtentsShape& Shape =
					Extents.Shapes.AddDefaulted_GetRef();
				if (bVehicle)
				{
					Shape.Shape = ESeinExtentsShape::Box;
					Shape.HalfExtentX = FFixedPoint::FromInt(150);
					Shape.HalfExtentY = FFixedPoint::FromInt(90);
				}
				else
				{
					Shape.Shape = ESeinExtentsShape::Capsule;
					Shape.Radius = FFixedPoint::FromInt(45);
				}

				const FFixedPoint Yaw = Index < 2
					? FFixedPoint::Zero
					: FFixedPoint::Pi;
				const FSeinEntityHandle Entity =
					Source->SpawnAbstractEntity(
						Pose(Starts[Index], Yaw),
						FSeinPlayerID::Neutral());
				Source->AddComponent(Entity, Movement);
				Source->AddComponent(Entity, Navigation);
				Source->AddComponent(Entity, Extents);
				Source->AddComponent(Entity, FSeinAbilityComponent());
				Entities.Add(Entity);
				AbilityIds.Add(USeinAbilityBPFL::SeinGrantAbility(
					Source,
					Entity,
					USeinVehicleGymAbility::StaticClass()));
			}
		},
		FSeinMatchSettings(),
		0x4D495854,
		TEXT("VehicleGym.MixedTraffic"),
		&Error)));
	ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source, &Error)));

	TArray<USeinMoveToProxy*> SourceProxies;
	{
		auto Scope = FSeinSimContextTestAccess::Enter(*Source);
		for (int32 Index = 0; Index < Entities.Num(); ++Index)
		{
			USeinAbility* Ability =
				Source->GetAbilityInstance(AbilityIds[Index]);
			ASSERT_THAT(IsNotNull(Ability));
			ASSERT_THAT(IsTrue(Ability->ActivateAbility(
				FSeinEntityHandle::Invalid(),
				Destinations[Index])));
			USeinMoveToProxy* Proxy = USeinMoveToProxy::SeinMoveTo(
				Ability, Destinations[Index]);
			ASSERT_THAT(IsNotNull(Proxy));
			Proxy->Activate();
			ASSERT_THAT(IsNotNull(
				UE::SeinARTSTests::
					FMoveToActionContinuationTestAccess::
					GetRunningAction(*Proxy)));
			SourceProxies.Add(Proxy);
		}
	}

	bool bReachedCloseTraffic = false;
	for (int32 Tick = 0; Tick < DriverTickRate * 10; ++Tick)
	{
		TickRunningWorlds(*Source, 1);
		for (int32 Lane = 0; Lane < 2; ++Lane)
		{
			const FSeinEntity* Vehicle = Source->GetEntity(Entities[Lane]);
			const FSeinEntity* Infantry =
				Source->GetEntity(Entities[Lane + 2]);
			if (!Vehicle || !Infantry)
			{
				continue;
			}
			FFixedVector Delta = Vehicle->Transform.GetLocation()
				- Infantry->Transform.GetLocation();
			Delta.Z = FFixedPoint::Zero;
			if (Delta.Size() < FFixedPoint::FromInt(450))
			{
				bReachedCloseTraffic = true;
				break;
			}
		}
		if (bReachedCloseTraffic)
		{
			break;
		}
	}
	ASSERT_THAT(IsTrue(bReachedCloseTraffic));
	ASSERT_THAT(AreEqual(
		4,
		Source->LatentActionManager->GetActiveActionCount()));

	Source->StopSimulation();
	FSeinWorldSnapshot Snapshot;
	Source->CaptureSnapshot(Snapshot);
	ASSERT_THAT(AreEqual(
		FSeinWorldSnapshot::CurrentVersion,
		Snapshot.SnapshotVersion));

	FActorTestSpawner DestinationSpawner;
	USeinWorldSubsystem* Destination = DestinationSpawner.GetWorld()
		.GetSubsystem<USeinWorldSubsystem>();
	ASSERT_THAT(IsNotNull(Destination));
	ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
		*Destination, Snapshot, &Error)));
	ASSERT_THAT(AreEqual(
		4,
		Destination->LatentActionManager->GetActiveActionCount()));
	ASSERT_THAT(IsTrue(Source->StartSimulation()));
	ASSERT_THAT(IsTrue(Destination->StartSimulation()));

	FGuid SourceRoot;
	FGuid DestinationRoot;
	ASSERT_THAT(IsTrue(ComputeRoot(*Source, SourceRoot)));
	ASSERT_THAT(IsTrue(ComputeRoot(*Destination, DestinationRoot)));
	ASSERT_THAT(AreEqual(SourceRoot, DestinationRoot));
	bool bBothCleared = false;
	for (int32 Tick = 0; Tick < DriverTickRate * 30; ++Tick)
	{
		TickRunningWorlds(*Source, 1);
		ASSERT_THAT(AreEqual(
			Source->GetCurrentTick(),
			Destination->GetCurrentTick()));
		ASSERT_THAT(IsTrue(ComputeRoot(*Source, SourceRoot)));
		ASSERT_THAT(IsTrue(ComputeRoot(*Destination, DestinationRoot)));
		ASSERT_THAT(AreEqual(SourceRoot, DestinationRoot));
		const int32 SourceActions =
			Source->LatentActionManager->GetActiveActionCount();
		ASSERT_THAT(AreEqual(
			SourceActions,
			Destination->LatentActionManager->GetActiveActionCount()));
		if (SourceActions == 0)
		{
			bBothCleared = true;
			break;
		}
	}
	Source->StopSimulation();
	Destination->StopSimulation();
	ASSERT_THAT(IsTrue(bBothCleared));
}
