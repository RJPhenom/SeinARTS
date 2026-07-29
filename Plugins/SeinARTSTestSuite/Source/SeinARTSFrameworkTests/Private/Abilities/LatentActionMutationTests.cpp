#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Abilities/SeinAbility.h"
#include "Abilities/SeinLatentActionManager.h"
#include "Components/SeinAbilityComponent.h"
#include "Data/SeinWorldSnapshot.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "TestTypes/SeinEffectMutationTestTypes.h"

USeinLatentMutationTestAction::FTickCallback USeinLatentMutationTestAction::TickCallback;
USeinLatentMutationTestAction::FCancelCallback USeinLatentMutationTestAction::CancelCallback;
USeinLatentMutationTestAction::FAbandonCallback
	USeinLatentMutationTestAction::AbandonCallback;

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

void USeinLatentMutationTestAction::OnTimelineAbandoned()
{
	++AbandonCount;
	if (AbandonCallback)
	{
		AbandonCallback(*this);
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
			USeinLatentMutationTestAction::AbandonCallback = nullptr;
		}
	};

	struct FLatentActionMutationFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		USeinLatentActionManager* Manager = nullptr;
		USeinEffectLedgerTestAbility* Ability = nullptr;
		FSeinEntityHandle Entity;
		int32 AbilityID = INDEX_NONE;

		bool Initialize()
		{
			World =
				Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World
				|| !SeinTestMatchBootstrap::Materialize(
					*World,
					[this]()
					{
						Entity = World->SpawnAbstractEntity(
							FFixedTransform(),
							FSeinPlayerID::Neutral());
						World->AddComponent(
							Entity,
							FSeinAbilityComponent());
						AbilityID =
							USeinAbilityBPFL::SeinGrantAbility(
								World,
								Entity,
								USeinEffectLedgerTestAbility::
									StaticClass());
					})
				|| !SeinTestMatchBootstrap::Start(*World))
			{
				return false;
			}

			Manager = World->LatentActionManager;
			Ability = Cast<USeinEffectLedgerTestAbility>(
				World->GetAbilityInstance(AbilityID));
			if (!Manager || !Ability || !Entity.IsValid())
			{
				return false;
			}
			auto SimScope =
				FSeinSimContextTestAccess::Enter(*World);
			return Ability->ActivateAbility(
				FSeinEntityHandle::Invalid(),
				FFixedVector::ZeroVector);
		}

		void Bind(USeinLatentAction& Action) const
		{
			Action.OwningAbility = Ability;
			Action.OwnerEntity = Entity;
		}
	};
}

namespace UE::SeinARTSTests
{
	TEST(LatentActionsRegisteredByATickStartOnTheNextTick, "SeinARTS.Unit.Abilities")
	{
		FScopedLatentCallbackReset Reset;
		FLatentActionMutationFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));
		USeinWorldSubsystem* World = Fixture.World;
		USeinLatentActionManager* Manager = Fixture.Manager;

		USeinLatentMutationTestAction* Parent = NewObject<USeinLatentMutationTestAction>(Manager);
		Fixture.Bind(*Parent);
		USeinLatentMutationTestAction* Child = nullptr;
		USeinLatentMutationTestAction::TickCallback =
			[&](USeinLatentMutationTestAction& Action, USeinWorldSubsystem&) -> bool
		{
			if (&Action == Parent)
			{
				Child = NewObject<USeinLatentMutationTestAction>(Manager);
				Fixture.Bind(*Child);
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

	TEST(LatentHardResetRejectsActionsRegisteredByCancelCallbacks,
		"SeinARTS.Unit.Snapshot.Latent.Manager")
	{
		FScopedLatentCallbackReset Reset;
		FLatentActionMutationFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));
		USeinLatentActionManager* Manager = Fixture.Manager;

		USeinLatentMutationTestAction* First = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction* Second = NewObject<USeinLatentMutationTestAction>(Manager);
		Fixture.Bind(*First);
		Fixture.Bind(*Second);
		USeinLatentMutationTestAction* SpawnedFromCancel = nullptr;
		bool bRegistrationAccepted = true;
		USeinLatentMutationTestAction::CancelCallback =
			[&](USeinLatentMutationTestAction& Action)
		{
			if (&Action == First)
			{
				SpawnedFromCancel = NewObject<USeinLatentMutationTestAction>(Manager);
				Fixture.Bind(*SpawnedFromCancel);
				bRegistrationAccepted =
					Manager->RegisterAction(SpawnedFromCancel);
			}
		};

		Manager->RegisterAction(First);
		Manager->RegisterAction(Second);
		TestRunner->AddExpectedError(
			TEXT("RegisterAction rejected invalid identity, ownership, or exhausted latent-action ID space."),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		Manager->CancelAllActions();

		ASSERT_THAT(AreEqual(1, First->CancelCount));
		ASSERT_THAT(AreEqual(1, Second->CancelCount));
		ASSERT_THAT(IsNotNull(SpawnedFromCancel));
		ASSERT_THAT(IsFalse(bRegistrationAccepted));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(0),
			SpawnedFromCancel->GetActionID()));
		ASSERT_THAT(AreEqual(0, SpawnedFromCancel->CancelCount));
		ASSERT_THAT(AreEqual(0, Manager->GetActiveActionCount()));
	}

	TEST(LatentRegistrationRejectsAnAbilityFromAnotherWorld,
		"SeinARTS.Unit.Snapshot.Latent.Manager")
	{
		FScopedLatentCallbackReset Reset;
		FLatentActionMutationFixture Local;
		FLatentActionMutationFixture Foreign;
		ASSERT_THAT(IsTrue(Local.Initialize()));
		ASSERT_THAT(IsTrue(Foreign.Initialize()));

		USeinLatentMutationTestAction* Action =
			NewObject<USeinLatentMutationTestAction>(Local.Manager);
		ASSERT_THAT(IsNotNull(Action));
		Foreign.Bind(*Action);
		TestRunner->AddExpectedError(
			TEXT("RegisterAction rejected invalid identity, ownership, or exhausted latent-action ID space."),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsFalse(Local.Manager->RegisterAction(Action)));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(0), Action->GetActionID()));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(0),
			Action->GetAbilityActivationID()));
		ASSERT_THAT(AreEqual(
			0, Local.Manager->GetActiveActionCount()));
	}

	TEST(LatentRestoreTeardownRejectsTimelineExtension,
		"SeinARTS.Unit.Snapshot.Latent.Manager")
	{
		FScopedLatentCallbackReset Reset;
		FLatentActionMutationFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));

		FSeinWorldSnapshot Snapshot;
		Fixture.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(
			FSeinWorldSnapshot::CurrentVersion,
			Snapshot.SnapshotVersion));

		USeinLatentMutationTestAction* OldAction =
			NewObject<USeinLatentMutationTestAction>(
				Fixture.Manager);
		Fixture.Bind(*OldAction);
		ASSERT_THAT(IsTrue(
			Fixture.Manager->RegisterAction(OldAction)));

		USeinLatentMutationTestAction* SpawnedFromAbandon =
			nullptr;
		bool bRegistrationAccepted = true;
		USeinLatentMutationTestAction::AbandonCallback =
			[&](USeinLatentMutationTestAction&)
		{
			SpawnedFromAbandon =
				NewObject<USeinLatentMutationTestAction>(
					Fixture.Manager);
			Fixture.Bind(*SpawnedFromAbandon);
			bRegistrationAccepted =
				Fixture.Manager->RegisterAction(
					SpawnedFromAbandon);
		};

		TestRunner->AddExpectedError(
			TEXT("RegisterAction rejected invalid identity, ownership, or exhausted latent-action ID space."),
			EAutomationExpectedErrorFlags::Contains,
			1,
			false);
		ASSERT_THAT(IsTrue(
			Fixture.World->RestoreSnapshot(Snapshot)));
		ASSERT_THAT(AreEqual(1, OldAction->AbandonCount));
		ASSERT_THAT(IsNotNull(SpawnedFromAbandon));
		ASSERT_THAT(IsFalse(bRegistrationAccepted));
		ASSERT_THAT(AreEqual(
			static_cast<int64>(0),
			SpawnedFromAbandon->GetActionID()));
		ASSERT_THAT(AreEqual(
			0, Fixture.Manager->GetActiveActionCount()));
		ASSERT_THAT(IsNull(
			Fixture.Ability->WorldSubsystem.Get()));
		ASSERT_THAT(AreEqual(
			INDEX_NONE,
			Fixture.Ability->GetRuntimePoolID()));
	}

	TEST(LatentSelfCancellationIsNotOverwrittenByTickCompletion, "SeinARTS.Unit.Abilities")
	{
		FScopedLatentCallbackReset Reset;
		FLatentActionMutationFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));
		USeinWorldSubsystem* World = Fixture.World;
		USeinLatentActionManager* Manager = Fixture.Manager;

		USeinLatentMutationTestAction* Action = NewObject<USeinLatentMutationTestAction>(Manager);
		Fixture.Bind(*Action);
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
		FLatentActionMutationFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));
		USeinWorldSubsystem* World = Fixture.World;
		USeinLatentActionManager* Manager = Fixture.Manager;

		USeinLatentMutationTestAction* First = NewObject<USeinLatentMutationTestAction>(Manager);
		USeinLatentMutationTestAction* Sibling = NewObject<USeinLatentMutationTestAction>(Manager);
		Fixture.Bind(*First);
		Fixture.Bind(*Sibling);
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
