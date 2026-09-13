#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SeinAbilityPayload.h"
#include "Events/SeinVisualEvent.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinActorBridgeSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinAbilityCallbackSafetyTestTypes.h"
#include "TestTypes/SeinAbilityVisualEventTestTypes.h"
#include "UObject/UnrealType.h"

void ASeinAbilityVisualEventTestActor::ProcessEvent(UFunction* Function, void* Parameters)
{
	if (Function && Parameters
		&& (Function->GetFName() == GET_FUNCTION_NAME_CHECKED(ASeinActor, ReceiveAbilityActivated)
			|| Function->GetFName() == GET_FUNCTION_NAME_CHECKED(ASeinActor, ReceiveAbilityEnded)))
	{
		if (const FStructProperty* TagProperty = FindFProperty<FStructProperty>(Function, TEXT("AbilityTag")))
		{
			ReceivedEvents.Add(Function->GetFName());
			ReceivedTags.Add(*TagProperty->ContainerPtrToValuePtr<FGameplayTag>(Parameters));
		}
	}
	Super::ProcessEvent(Function, Parameters);
}

namespace UE::SeinARTSTests
{
	namespace AbilityVisualEventTestLocal
	{
		struct FFixture
		{
			FActorTestSpawner Spawner;
			USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			FSeinEntityHandle Entity;
			USeinAbility* Ability = nullptr;

			bool Initialize(TSubclassOf<USeinAbility> AbilityClass)
			{
				if (!World || !SeinTestMatchBootstrap::Materialize(*World, [&]()
				{
					const FSeinPlayerID Player(1);
					World->RegisterPlayer(Player, FSeinFactionID(1));
					Entity = World->SpawnAbstractEntity(FFixedTransform(), Player);
					World->AddComponent(Entity, FSeinAbilityPayload());
					const int32 ID = USeinAbilityBPFL::SeinGrantAbility(World, Entity, AbilityClass);
					Ability = World->GetAbilityInstance(ID);
					if (Ability) Ability->AbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
				}) || !Ability || !SeinTestMatchBootstrap::Start(*World)) return false;
				World->FlushVisualEvents();
				return true;
			}

			bool Activate()
			{
				return Ability->ActivateAbility(FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector);
			}

			TArray<FSeinVisualEvent> Drain()
			{
				return World->FlushVisualEvents().FilterByPredicate([](const FSeinVisualEvent& Event)
				{
					return Event.Type == ESeinVisualEventType::AbilityActivated
						|| Event.Type == ESeinVisualEventType::AbilityEnded;
				});
			}

			~FFixture() { if (World) World->StopSimulation(); }
		};

		template <typename TTestRunner>
		void ExpectAbilityHashDiagnostic(TTestRunner& Runner)
		{
			Runner.AddExpectedError(
				TEXT("Component 'SeinAbilityPayload' has field(s) excluded from the legacy local state fingerprint"),
				EAutomationExpectedErrorFlags::Contains, 1, false);
		}
	}

	TEST(CompletionAndCancellationEmitOneOrderedPair, "SeinARTS.Unit.Abilities.VisualEvents")
	{
		AbilityVisualEventTestLocal::ExpectAbilityHashDiagnostic(*TestRunner);
		AbilityVisualEventTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(USeinCallbackCancelReplacementAbility::StaticClass())));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		for (const bool bCancel : {false, true})
		{
			ASSERT_THAT(IsTrue(F.Activate()));
			ASSERT_THAT(IsFalse(F.Activate())); // An already-active call must not emit again.
			if (bCancel) F.Ability->CancelAbility();
			else F.Ability->EndAbility();
			F.Ability->EndAbility();
			F.Ability->CancelAbility();
			const auto Events = F.Drain();
			ASSERT_THAT(AreEqual(2, Events.Num()));
			ASSERT_THAT(IsTrue(Events[0].Type == ESeinVisualEventType::AbilityActivated));
			ASSERT_THAT(IsTrue(Events[1].Type == ESeinVisualEventType::AbilityEnded));
			for (const auto& Event : Events)
			{
				ASSERT_THAT(IsTrue(Event.PrimaryEntity == F.Entity));
				ASSERT_THAT(IsTrue(Event.Tag == F.Ability->AbilityTag));
			}
		}
	}

	TEST(ImmediateCompletionKeepsActivationBeforeEnd, "SeinARTS.Unit.Abilities.VisualEvents")
	{
		AbilityVisualEventTestLocal::ExpectAbilityHashDiagnostic(*TestRunner);
		AbilityVisualEventTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(USeinCallbackImmediateEndAbility::StaticClass())));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(F.Activate()));
		ASSERT_THAT(IsFalse(F.Ability->bIsActive));
		const auto Events = F.Drain();
		ASSERT_THAT(AreEqual(2, Events.Num()));
		ASSERT_THAT(IsTrue(Events[0].Type == ESeinVisualEventType::AbilityActivated));
		ASSERT_THAT(IsTrue(Events[1].Type == ESeinVisualEventType::AbilityEnded));
	}

	TEST(CancelCallbackReplacementStartsAfterOldEnd, "SeinARTS.Unit.Abilities.VisualEvents")
	{
		AbilityVisualEventTestLocal::ExpectAbilityHashDiagnostic(*TestRunner);
		AbilityVisualEventTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(USeinCallbackRevokeOnCancelAbility::StaticClass())));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(F.Activate()));
		F.Drain();
		F.Ability->CancelAbility();
		const auto Events = F.Drain();
		ASSERT_THAT(AreEqual(2, Events.Num()));
		ASSERT_THAT(IsTrue(Events[0].Type == ESeinVisualEventType::AbilityEnded));
		ASSERT_THAT(IsTrue(Events[0].Tag == SeinARTSTags::Command_Context_AbilityTriggered));
		ASSERT_THAT(IsTrue(Events[1].Type == ESeinVisualEventType::AbilityActivated));
		ASSERT_THAT(IsTrue(Events[1].Tag == SeinARTSTags::Command_Context_Target_Ground));
		ASSERT_THAT(IsTrue(Events[0].PrimaryEntity == F.Entity));
		ASSERT_THAT(IsTrue(Events[1].PrimaryEntity == F.Entity));
	}

	TEST(RejectedPrimaryActivationEmitsNoLifecycleEvents, "SeinARTS.Unit.Abilities.VisualEvents")
	{
		AbilityVisualEventTestLocal::ExpectAbilityHashDiagnostic(*TestRunner);
		AbilityVisualEventTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(USeinCallbackCancelReplacementAbility::StaticClass())));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(F.Activate()));
		F.Drain();
		const int32 ID = USeinAbilityBPFL::SeinGrantAbility(
			F.World, F.Entity, USeinCallbackImmediateEndAbility::StaticClass());
		USeinAbility* Rejected = F.World->GetAbilityInstance(ID);
		ASSERT_THAT(IsNotNull(Rejected));
		TestRunner->AddExpectedError(TEXT("already has active primary"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(Rejected->ActivateAbility(
			FSeinEntityHandle::Invalid(), FFixedVector::ZeroVector)));
		Rejected->EndAbility();
		ASSERT_THAT(AreEqual(0, F.Drain().Num()));
	}

	TEST(ActorBridgeDeliversLifecycleToBlueprintEventEntrypoints, "SeinARTS.Unit.Abilities.VisualEvents")
	{
		AbilityVisualEventTestLocal::ExpectAbilityHashDiagnostic(*TestRunner);
		AbilityVisualEventTestLocal::FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(USeinCallbackCancelReplacementAbility::StaticClass())));
		auto* Actor = &F.Spawner.SpawnActor<ASeinAbilityVisualEventTestActor>();
		ASSERT_THAT(IsNotNull(Actor));
		auto* Bridge = F.Spawner.GetWorld().GetSubsystem<USeinActorBridgeSubsystem>();
		ASSERT_THAT(IsNotNull(Bridge));
		Bridge->RegisterActor(F.Entity, Actor);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			ASSERT_THAT(IsTrue(F.Activate()));
		}
		ASSERT_THAT(AreEqual(0, Actor->ReceivedEvents.Num()));
		Bridge->Tick(0.0f);
		ASSERT_THAT(AreEqual(1, Actor->ReceivedEvents.Num()));
		ASSERT_THAT(IsTrue(Actor->ReceivedEvents[0]
			== GET_FUNCTION_NAME_CHECKED(ASeinActor, ReceiveAbilityActivated)));
		ASSERT_THAT(IsTrue(Actor->ReceivedTags[0] == F.Ability->AbilityTag));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			F.Ability->CancelAbility();
		}
		Bridge->Tick(0.0f);
		ASSERT_THAT(AreEqual(2, Actor->ReceivedEvents.Num()));
		ASSERT_THAT(IsTrue(Actor->ReceivedEvents[1]
			== GET_FUNCTION_NAME_CHECKED(ASeinActor, ReceiveAbilityEnded)));
		ASSERT_THAT(IsTrue(Actor->ReceivedTags[1] == F.Ability->AbilityTag));
		Bridge->Tick(0.0f);
		ASSERT_THAT(AreEqual(2, Actor->ReceivedEvents.Num()));
		Bridge->UnregisterActor(F.Entity);
	}
}
