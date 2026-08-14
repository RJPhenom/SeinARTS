#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actions/SeinMoveToAction.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"
#include "Types/Entity.h"

bool USeinMoveToLifecycleTestMovement::bFinishOnTick = false;
int32 USeinMoveToLifecycleTestMovement::BeginCount = 0;
int32 USeinMoveToLifecycleTestMovement::TickCount = 0;
int32 USeinMoveToLifecycleTestMovement::EndCount = 0;
int32 USeinMoveToLifecycleTestMovement::PlanPathCallCount = 0;
int32 USeinMoveToLifecycleTestMovement::LastTickPathWaypointCount = 0;
FFixedVector USeinMoveToLifecycleTestMovement::RepathWaypointMarker =
	FFixedVector::ZeroVector;
FFixedVector USeinMoveToLifecycleTestMovement::LastTickMiddleWaypoint =
	FFixedVector::ZeroVector;
TArray<ESeinPathResult>
	USeinMoveToLifecycleTestMovement::ScriptedPathResults;
TArray<int32> USeinMoveToLifecycleTestMovement::EmptyFoundCallIndices;
bool USeinMoveToLifecycleTestMovement::bRepathPathsPartial = false;
bool USeinMoveToLifecycleTestMovement::bInitialPathSkipsStart = false;
TFunction<void()> USeinMoveToLifecycleTestMovement::MoveEndCallback;

void USeinMoveToLifecycleTestMovement::Reset()
{
	bFinishOnTick = false;
	BeginCount = 0;
	TickCount = 0;
	EndCount = 0;
	PlanPathCallCount = 0;
	LastTickPathWaypointCount = 0;
	RepathWaypointMarker = FFixedVector::ZeroVector;
	LastTickMiddleWaypoint = FFixedVector::ZeroVector;
	ScriptedPathResults.Reset();
	EmptyFoundCallIndices.Reset();
	bRepathPathsPartial = false;
	bInitialPathSkipsStart = false;
	MoveEndCallback = nullptr;
}

ESeinPathResult USeinMoveToLifecycleTestMovement::PlanPath(
	const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const
{
	const int32 CallIndex = PlanPathCallCount++;
	const ESeinPathResult Result = ScriptedPathResults.IsValidIndex(CallIndex)
		? ScriptedPathResults[CallIndex]
		: ESeinPathResult::Found;
	OutPath.Clear();
	if (Result != ESeinPathResult::Found)
	{
		return Result;
	}
	if (EmptyFoundCallIndices.Contains(CallIndex))
	{
		return ESeinPathResult::Found;
	}
	const FFixedVector Start = Ctx.Entity.Transform.GetLocation();
	if (CallIndex == 0 && bInitialPathSkipsStart)
	{
		OutPath.Waypoints.Add(FFixedVector(
			(Start.X + Ctx.Destination.X) / FFixedPoint::FromInt(2),
			(Start.Y + Ctx.Destination.Y) / FFixedPoint::FromInt(2),
			(Start.Z + Ctx.Destination.Z) / FFixedPoint::FromInt(2)));
	}
	else
	{
		OutPath.Waypoints.Add(Start);
	}
	if (CallIndex > 0 && RepathWaypointMarker != FFixedVector::ZeroVector)
	{
		OutPath.Waypoints.Add(RepathWaypointMarker);
	}
	OutPath.Waypoints.Add(Ctx.Destination);
	OutPath.bIsValid = true;
	OutPath.bIsPartial = CallIndex > 0 && bRepathPathsPartial;
	OutPath.DeriveSegmentsFromWaypoints();
	return ESeinPathResult::Found;
}

void USeinMoveToLifecycleTestMovement::OnMoveBegin(
	const FSeinMovementContext&)
{
	++BeginCount;
}

bool USeinMoveToLifecycleTestMovement::Tick(
	const FSeinMovementContext& Ctx)
{
	++TickCount;
	LastTickPathWaypointCount = Ctx.Path.Waypoints.Num();
	LastTickMiddleWaypoint = Ctx.Path.Waypoints.Num() > 2
		? Ctx.Path.Waypoints[1]
		: FFixedVector::ZeroVector;
	return bFinishOnTick;
}

void USeinMoveToLifecycleTestMovement::OnMoveEnd(FSeinEntity&)
{
	++EndCount;
	if (MoveEndCallback)
	{
		MoveEndCallback();
	}
}

void USeinMoveToLifecycleTestObserver::HandleCompleted(
	FSeinMoveToResult Result)
{
	++CompletedCount;
	bCompletedSawTerminalAction = Action
		&& Action->bCompleted
		&& !Action->bCancelled
		&& !Action->bFailed;
	if (Ability)
	{
		Ability->EndAbility();
	}
}

void USeinMoveToLifecycleTestObserver::HandleFailed(
	FSeinMoveToResult Result)
{
	++FailedCount;
	LastFailure = Result.FailureReason;
	bFailedSawTerminalAction = Action
		&& Action->bCompleted
		&& Action->bFailed
		&& !Action->bCancelled;
	if (Ability)
	{
		Ability->EndAbility();
	}
}

void USeinMoveToLifecycleTestObserver::HandleCancelled(
	FSeinMoveToResult Result)
{
	++CancelledCount;
	LastFailure = Result.FailureReason;
	if (bReenterCancellationOnCancelled && Manager && Ability)
	{
		Manager->CancelActionsForAbility(Ability);
	}
}

void USeinMoveToLifecycleTestObserver::HandlePathRecomputed(
	FSeinMoveToResult)
{
	++PathRecomputedCount;
	RepathEventOrder.Add(1);
	RecomputedObservedRepathElapsed = Action
		? UE::SeinARTSTests::FMoveToActionContinuationTestAccess::
			GetRepathElapsed(*Action)
		: FFixedPoint::MinValue;
}

void USeinMoveToLifecycleTestObserver::HandlePartialPath(
	FSeinMoveToResult)
{
	++PartialPathCount;
	RepathEventOrder.Add(2);
}

namespace
{
	struct FScopedDisabledNavigation
	{
		FScopedDisabledNavigation()
			: Settings(GetMutableDefault<USeinARTSCoreSettings>())
			, SavedNavigationClass(Settings
				? Settings->NavigationClass
				: FSoftClassPath())
		{
			check(Settings);
			Settings->NavigationClass.Reset();
		}

		~FScopedDisabledNavigation()
		{
			Settings->NavigationClass = SavedNavigationClass;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		FSoftClassPath SavedNavigationClass;
	};

	struct FScopedMoveToTestState
	{
		FScopedMoveToTestState()
		{
			USeinMoveToLifecycleTestMovement::Reset();
		}

		~FScopedMoveToTestState()
		{
			USeinMoveToLifecycleTestMovement::Reset();
		}
	};

	struct FMoveToLifecycleFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		USeinLatentActionManager* Manager = nullptr;
		USeinMoveToLifecycleTestAbility* Ability = nullptr;
		USeinMoveToAction* Action = nullptr;
		USeinMoveToProxy* Proxy = nullptr;
		USeinMoveToLifecycleTestObserver* Observer = nullptr;
		FSeinEntityHandle Entity;
		int32 AbilityID = INDEX_NONE;

		bool Initialize(
			bool bFinishOnFirstTick,
			const FSeinNavigationComponent* NavigationComponent = nullptr)
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				return false;
			}

			const bool bMaterialized = SeinTestMatchBootstrap::Materialize(
				*World, [&]()
				{
					Entity = World->SpawnAbstractEntity(
						FFixedTransform(), FSeinPlayerID::Neutral());
					FSeinMovementComponent MovementComponent;
					MovementComponent.MovementClass = FSoftClassPath(
						USeinMoveToLifecycleTestMovement::StaticClass()->GetPathName());
					World->AddComponent(Entity, MovementComponent);
					if (NavigationComponent)
					{
						World->AddComponent(Entity, *NavigationComponent);
					}
					World->AddComponent(Entity, FSeinAbilityComponent());
					AbilityID = USeinAbilityBPFL::SeinGrantAbility(
						World, Entity,
						USeinMoveToLifecycleTestAbility::StaticClass());
				});
			if (!bMaterialized || !Entity.IsValid()
				|| !SeinTestMatchBootstrap::Start(*World))
			{
				return false;
			}

			Manager = World->LatentActionManager;
			if (!Manager)
			{
				return false;
			}

			Ability = Cast<USeinMoveToLifecycleTestAbility>(
				World->GetAbilityInstance(AbilityID));
			if (!Ability)
			{
				return false;
			}
			{
				auto SimScope = FSeinSimContextTestAccess::Enter(*World);
				if (!Ability->ActivateAbility(
					FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector))
				{
					return false;
				}
			}

			Proxy = NewObject<USeinMoveToProxy>(World);
			Action = NewObject<USeinMoveToAction>(Proxy);
			Observer = NewObject<USeinMoveToLifecycleTestObserver>(Proxy);
			if (!Proxy || !Action || !Observer)
			{
				return false;
			}

			Action->OwningAbility = Ability;
			Action->OwnerEntity = Entity;
			Action->Observer = Proxy;
			Action->Initialize(FFixedVector(
				FFixedPoint::FromInt(100),
				FFixedPoint::Zero,
				FFixedPoint::Zero));

			Observer->Ability = Ability;
			Observer->Action = Action;
			Observer->Manager = Manager;
			Proxy->OnCompleted.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandleCompleted);
			Proxy->OnFailed.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandleFailed);
			Proxy->OnCancelled.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandleCancelled);
			Proxy->OnPathRecomputed.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandlePathRecomputed);
			Proxy->OnPartialPath.AddDynamic(
				Observer, &USeinMoveToLifecycleTestObserver::HandlePartialPath);

			USeinMoveToLifecycleTestMovement::bFinishOnTick =
				bFinishOnFirstTick;
			Manager->RegisterAction(Action);
			return true;
		}

		void Tick(FFixedPoint DeltaTime = FFixedPoint::One)
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Manager->TickAll(DeltaTime, *World);
		}

		void SetLocation(const FFixedVector& Location)
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			if (FSeinEntity* SimEntity = World->GetEntityMutable(Entity))
			{
				SimEntity->Transform.SetLocation(Location);
			}
		}
	};
}

namespace UE::SeinARTSTests
{
	TEST(MoveToCompletedCallbackCanEndAbilityWithoutCancellation,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true)));

		Fixture.Tick();

		ASSERT_THAT(IsTrue(Fixture.Observer->bCompletedSawTerminalAction));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CompletedCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->FailedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsFalse(Fixture.Action->bCancelled));
		ASSERT_THAT(IsFalse(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));

		const FSeinMovementComponent* Movement =
			Fixture.World->GetComponent<FSeinMovementComponent>(Fixture.Entity);
		ASSERT_THAT(IsNotNull(Movement));
		ASSERT_THAT(IsFalse(Movement->bHasTarget));
		ASSERT_THAT(IsFalse(Movement->bArrivalImminent));
	}

	TEST(MoveToCancellationFinalizesMovementOnceUnderReentry,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));
		Fixture.Tick();
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));

		Fixture.Observer->bReenterCancellationOnCancelled = true;
		USeinMoveToLifecycleTestMovement::MoveEndCallback = [&]()
		{
			Fixture.Manager->CancelActionsForAbility(Fixture.Ability);
		};
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.Ability->CancelAbility();
		}
		Fixture.Manager->CleanupCompleted();

		ASSERT_THAT(IsTrue(Fixture.Action->bCancelled));
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::Cancelled),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CompletedCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));
	}

	TEST(MoveToFailureFinalizesMovementOnceAndRemainsFailure,
		"SeinARTS.Sim.Movement.Lifecycle")
	{
		FScopedMoveToTestState Reset;
		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false)));
		Fixture.Tick();
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));

		USeinMoveToLifecycleTestMovement::MoveEndCallback = [&]()
		{
			Fixture.Manager->CancelActionsForAbility(Fixture.Ability);
		};
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Fixture.World->RemoveComponent<FSeinMovementComponent>(
				Fixture.Entity);
			Fixture.Manager->TickAll(FFixedPoint::FromInt(1), *Fixture.World);
		}

		ASSERT_THAT(IsTrue(Fixture.Observer->bFailedSawTerminalAction));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::NoMovementComponent),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CancelledCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->CompletedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(IsFalse(Fixture.Action->bCancelled));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::EndCount));
		ASSERT_THAT(IsFalse(Fixture.Ability->bIsActive));
		ASSERT_THAT(AreEqual(0, Fixture.Manager->GetActiveActionCount()));
	}

	TEST(MoveToIntervalRepathCommitsBeforeMovementTick,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(20);
		const FFixedVector Marker(
			FFixedPoint::FromInt(40),
			FFixedPoint::FromInt(20),
			FFixedPoint::Zero);
		USeinMoveToLifecycleTestMovement::RepathWaypointMarker = Marker;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(FFixedPoint::One / FFixedPoint::FromInt(20));

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::LastTickPathWaypointCount));
		ASSERT_THAT(IsTrue(
			USeinMoveToLifecycleTestMovement::LastTickMiddleWaypoint == Marker));
	}

	TEST(MoveToIntervalThrottleWaitsFullCadenceBeforeRetry,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(8);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Throttled,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint HalfInterval =
			FFixedPoint::One / FFixedPoint::FromInt(16);
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(HalfInterval);
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
	}

	TEST(MoveToForcedIntervalRepathBypassesCadenceAndConsumesThrottle,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Throttled
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);
		Fixture.Tick(FFixedPoint::Epsilon);

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Zero));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToForcedOffPathRepathBypassesCadenceAndDrift,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10000);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);
		Fixture.Tick(FFixedPoint::Epsilon);

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(IsFalse(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
	}

	TEST(MoveToForcedRepathRemainsPendingWithoutNavigation,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FScopedDisabledNavigation DisabledNavigation;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval = FFixedPoint::Epsilon;

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);
		Fixture.Tick(FFixedPoint::Epsilon);

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Epsilon));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToForcedRepathRemainsPendingWithoutNavigationSubsystem,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval = FFixedPoint::FromInt(10);

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		FMoveToActionContinuationTestAccess::SetForceRepathPending(
			*Fixture.Action, true);

		{
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*Fixture.World);
			ASSERT_THAT(IsTrue(
				FMoveToActionContinuationTestAccess::
					TickRepathWithoutNavigationSubsystem(
						*Fixture.Action,
						FFixedPoint::Epsilon,
						*Fixture.World)));
		}

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::IsForceRepathPending(
				*Fixture.Action)));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Epsilon + FFixedPoint::Epsilon));
	}

	TEST(MoveToIntervalRepathFailsAtConfiguredLimit,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(20);
		Navigation.RepathFailureLimit = 2;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::NotFound,
			ESeinPathResult::NotFound
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint Interval =
			FFixedPoint::One / FFixedPoint::FromInt(20);
		Fixture.Tick(Interval);
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		Fixture.Tick(Interval);

		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::PathNotFound),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToOffPathRepathRequiresDriftAndMinimumCadence,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint MinimumAttempt =
			FFixedPoint::One / FFixedPoint::FromInt(10);
		Fixture.Tick(MinimumAttempt);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero));
		Fixture.Tick(MinimumAttempt - FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
	}

	TEST(MoveToOffPathImplicitOriginPrefixPreventsFalseDrift,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::bInitialPathSkipsStart = true;

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(FFixedPoint::One / FFixedPoint::FromInt(10));

		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToOffPathThrottleWaitsMinimumCadenceBeforeRetry,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Throttled,
			ESeinPathResult::Found
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint MinimumAttempt =
			FFixedPoint::One / FFixedPoint::FromInt(10);
		Fixture.Tick(MinimumAttempt);
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero));
		Fixture.Tick(MinimumAttempt);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));
		Fixture.Tick(MinimumAttempt - FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		Fixture.Tick(FFixedPoint::Epsilon);
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
	}

	TEST(MoveToEmptyFoundAndNoNavigationCountAsRepathFailures,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(16);
		Navigation.RepathFailureLimit = 2;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found,
			ESeinPathResult::NoNavigation
		};
		USeinMoveToLifecycleTestMovement::EmptyFoundCallIndices = {1};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(Navigation.RepathInterval);
		ASSERT_THAT(IsFalse(Fixture.Action->bCompleted));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::LastTickPathWaypointCount));
		ASSERT_THAT(AreEqual(0, Fixture.Observer->PathRecomputedCount));

		Fixture.Tick(Navigation.RepathInterval);
		ASSERT_THAT(AreEqual(
			3, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(AreEqual(
			static_cast<int32>(ESeinMoveFailureReason::PathNotFound),
			static_cast<int32>(Fixture.Observer->LastFailure)));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToPartialRepathEmitsEventsInOrderWithoutRebegin,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::Interval;
		Navigation.RepathInterval =
			FFixedPoint::One / FFixedPoint::FromInt(16);
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::Found
		};
		USeinMoveToLifecycleTestMovement::bRepathPathsPartial = true;

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		Fixture.Tick(Navigation.RepathInterval);

		ASSERT_THAT(AreEqual(1, Fixture.Observer->PathRecomputedCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->PartialPathCount));
		ASSERT_THAT(AreEqual(2, Fixture.Observer->RepathEventOrder.Num()));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->RepathEventOrder[0]));
		ASSERT_THAT(AreEqual(2, Fixture.Observer->RepathEventOrder[1]));
		ASSERT_THAT(IsTrue(
			Fixture.Observer->RecomputedObservedRepathElapsed
				== Navigation.RepathInterval));
		ASSERT_THAT(IsTrue(
			FMoveToActionContinuationTestAccess::GetRepathElapsed(
				*Fixture.Action) == FFixedPoint::Zero));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::BeginCount));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}

	TEST(MoveToOffPathRepathFailsBeforeMovementAtLimit,
		"SeinARTS.Sim.Movement.Repath")
	{
		FScopedMoveToTestState Reset;
		FSeinNavigationComponent Navigation;
		Navigation.RepathMode = ESeinRepathMode::OffPathOnly;
		Navigation.OffPathThreshold = FFixedPoint::FromInt(10);
		Navigation.RepathFailureLimit = 1;
		USeinMoveToLifecycleTestMovement::ScriptedPathResults = {
			ESeinPathResult::Found,
			ESeinPathResult::NotFound
		};

		FMoveToLifecycleFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(false, &Navigation)));
		const FFixedPoint MinimumAttempt =
			FFixedPoint::One / FFixedPoint::FromInt(10);
		Fixture.Tick(MinimumAttempt);
		Fixture.SetLocation(FFixedVector(
			FFixedPoint::Zero,
			FFixedPoint::FromInt(50),
			FFixedPoint::Zero));
		Fixture.Tick(MinimumAttempt);

		ASSERT_THAT(AreEqual(
			2, USeinMoveToLifecycleTestMovement::PlanPathCallCount));
		ASSERT_THAT(AreEqual(1, Fixture.Observer->FailedCount));
		ASSERT_THAT(IsTrue(Fixture.Action->bCompleted));
		ASSERT_THAT(IsTrue(Fixture.Action->bFailed));
		ASSERT_THAT(AreEqual(
			1, USeinMoveToLifecycleTestMovement::TickCount));
	}
}
