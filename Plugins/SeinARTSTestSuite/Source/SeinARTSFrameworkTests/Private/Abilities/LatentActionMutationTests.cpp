#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinLatentActionManager.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinEffectMutationTestTypes.h"

USeinLatentMutationTestAction::FTickCallback USeinLatentMutationTestAction::TickCallback;
USeinLatentMutationTestAction::FCancelCallback USeinLatentMutationTestAction::CancelCallback;

bool USeinLatentMutationTestAction::TickAction(FFixedPoint, USeinWorldSubsystem& World)
{
	++TickCount;
	return TickCallback ? TickCallback(*this, World) : true;
}

void USeinLatentMutationTestAction::OnCancel()
{
	++CancelCount;
	if (CancelCallback)
	{
		CancelCallback(*this);
	}
}

namespace
{
	struct FScopedLatentCallbackReset
	{
		~FScopedLatentCallbackReset()
		{
			USeinLatentMutationTestAction::TickCallback = nullptr;
			USeinLatentMutationTestAction::CancelCallback = nullptr;
		}
	};
}

namespace UE::SeinARTSTests
{
	TEST(LatentActionsRegisteredByATickStartOnTheNextTick, "SeinARTS.Unit.Abilities")
	{
		FScopedLatentCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		USeinLatentActionManager* Manager = NewObject<USeinLatentActionManager>(World);
		ASSERT_THAT(IsNotNull(Manager));

		USeinLatentMutationTestAction* Parent = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction* Child = nullptr;
		USeinLatentMutationTestAction::TickCallback =
			[&](USeinLatentMutationTestAction& Action, USeinWorldSubsystem&) -> bool
		{
			if (&Action == Parent)
			{
				Child = NewObject<USeinLatentMutationTestAction>(Manager);
				Manager->RegisterAction(Child);
			}
			return true;
		};

		Manager->RegisterAction(Parent);
		Manager->TickAll(FFixedPoint::One, *World);
		ASSERT_THAT(IsNotNull(Child));
		ASSERT_THAT(AreEqual(1, Parent->TickCount));
		ASSERT_THAT(AreEqual(0, Child->TickCount));
		ASSERT_THAT(AreEqual(1, Manager->GetActiveActionCount()));

		Manager->TickAll(FFixedPoint::One, *World);
		ASSERT_THAT(AreEqual(1, Child->TickCount));
		ASSERT_THAT(AreEqual(0, Manager->GetActiveActionCount()));
	}

	TEST(LatentHardResetDiscardsActionsRegisteredByCancelCallbacks, "SeinARTS.Unit.Abilities")
	{
		FScopedLatentCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		USeinLatentActionManager* Manager = NewObject<USeinLatentActionManager>(World);
		ASSERT_THAT(IsNotNull(Manager));

		USeinLatentMutationTestAction* First = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction* Second = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction* SpawnedFromCancel = nullptr;
		USeinLatentMutationTestAction::CancelCallback =
			[&](USeinLatentMutationTestAction& Action)
		{
			if (&Action == First)
			{
				SpawnedFromCancel = NewObject<USeinLatentMutationTestAction>(Manager);
				Manager->RegisterAction(SpawnedFromCancel);
			}
		};

		Manager->RegisterAction(First);
		Manager->RegisterAction(Second);
		Manager->CancelAllActions();

		ASSERT_THAT(AreEqual(1, First->CancelCount));
		ASSERT_THAT(AreEqual(1, Second->CancelCount));
		ASSERT_THAT(IsNotNull(SpawnedFromCancel));
		ASSERT_THAT(AreEqual(0, SpawnedFromCancel->CancelCount));
		ASSERT_THAT(AreEqual(0, Manager->GetActiveActionCount()));
	}

	TEST(LatentSelfCancellationIsNotOverwrittenByTickCompletion, "SeinARTS.Unit.Abilities")
	{
		FScopedLatentCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		USeinLatentActionManager* Manager = NewObject<USeinLatentActionManager>(World);
		ASSERT_THAT(IsNotNull(Manager));

		USeinLatentMutationTestAction* Action = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction::TickCallback =
			[](USeinLatentMutationTestAction& TickingAction, USeinWorldSubsystem&) -> bool
		{
			TickingAction.Cancel();
			return true;
		};

		Manager->RegisterAction(Action);
		Manager->TickAll(FFixedPoint::One, *World);
		ASSERT_THAT(IsTrue(Action->bCancelled));
		ASSERT_THAT(IsFalse(Action->bCompleted));
		ASSERT_THAT(AreEqual(1, Action->CancelCount));
		ASSERT_THAT(AreEqual(0, Manager->GetActiveActionCount()));
	}

	TEST(LatentRecursiveHardResetCancelsTheTickingActionAndItsSiblingOnce, "SeinARTS.Unit.Abilities")
	{
		FScopedLatentCallbackReset Reset;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		USeinLatentActionManager* Manager = NewObject<USeinLatentActionManager>(World);
		ASSERT_THAT(IsNotNull(Manager));

		USeinLatentMutationTestAction* First = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction* Sibling = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction::TickCallback =
			[&](USeinLatentMutationTestAction& Action, USeinWorldSubsystem&) -> bool
		{
			if (&Action == First)
			{
				Manager->CancelAllActions();
			}
			return true;
		};
		USeinLatentMutationTestAction::CancelCallback =
			[&](USeinLatentMutationTestAction& Action)
		{
			if (&Action == First)
			{
				// Re-entry sees the manager's already-detached list and is a no-op.
				Manager->CancelAllActions();
			}
		};

		Manager->RegisterAction(First);
		Manager->RegisterAction(Sibling);
		Manager->TickAll(FFixedPoint::One, *World);

		ASSERT_THAT(IsTrue(First->bCancelled));
		ASSERT_THAT(IsFalse(First->bCompleted));
		ASSERT_THAT(AreEqual(1, First->CancelCount));
		ASSERT_THAT(IsTrue(Sibling->bCancelled));
		ASSERT_THAT(IsFalse(Sibling->bCompleted));
		ASSERT_THAT(AreEqual(1, Sibling->CancelCount));
		ASSERT_THAT(AreEqual(0, Sibling->TickCount));
		ASSERT_THAT(AreEqual(0, Manager->GetActiveActionCount()));
	}
}
