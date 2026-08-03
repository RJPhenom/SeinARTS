#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actions/SeinMoveToAction.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinMoveToLifecycleTestTypes.h"
#include "Types/Entity.h"

bool USeinMoveToLifecycleTestMovement::bFinishOnTick = false;
int32 USeinMoveToLifecycleTestMovement::BeginCount = 0;
int32 USeinMoveToLifecycleTestMovement::TickCount = 0;
int32 USeinMoveToLifecycleTestMovement::EndCount = 0;
TFunction<void()> USeinMoveToLifecycleTestMovement::MoveEndCallback;

void USeinMoveToLifecycleTestMovement::Reset()
{
	bFinishOnTick = false;
	BeginCount = 0;
	TickCount = 0;
	EndCount = 0;
	MoveEndCallback = nullptr;
}

ESeinPathResult USeinMoveToLifecycleTestMovement::PlanPath(
	const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const
{
	OutPath.Clear();
	OutPath.Waypoints.Add(Ctx.Entity.Transform.GetLocation());
	OutPath.Waypoints.Add(Ctx.Destination);
	OutPath.bIsValid = true;
	OutPath.bIsPartial = false;
	OutPath.DeriveSegmentsFromWaypoints();
	return ESeinPathResult::Found;
}

void USeinMoveToLifecycleTestMovement::OnMoveBegin(
	const FSeinMovementContext&)
{
	++BeginCount;
}

bool USeinMoveToLifecycleTestMovement::Tick(
	const FSeinMovementContext&)
{
	++TickCount;
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

namespace
{
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

		bool Initialize(bool bFinishOnFirstTick)
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

			USeinMoveToLifecycleTestMovement::bFinishOnTick =
				bFinishOnFirstTick;
			Manager->RegisterAction(Action);
			return true;
		}

		void Tick()
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Manager->TickAll(FFixedPoint::FromInt(1), *World);
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
}
