#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Actor/SeinActor.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinSquadPayload.h"
#include "Containers/Ticker.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Movement/SeinVehicleGymTestTypes.h"
#include "SeinSquadMutationBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace UE::SeinARTSTests
{
	TEST(SquadBrokerEnablesSharingAcrossCreationAndSlotReplacement,
		"SeinARTS.Integration.CooldownSharing")
	{
		TestRunner->AddExpectedError(TEXT("Component 'SeinAbilityPayload' has field(s) excluded from the legacy local state fingerprint"),
			EAutomationExpectedErrorFlags::Contains, 0, false);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Squad;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			Squad = World->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID::Neutral());
			FSeinSquadPayload Data;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				FSeinSquadSlot& Slot = Data.Slots.AddDefaulted_GetRef();
				Slot.Entity = ASeinActor::StaticClass();
			}
			World->AddComponent(Squad, Data);
		})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		const FSeinCommandBrokerData* Broker = World->GetComponent<FSeinCommandBrokerData>(Squad);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsTrue(Broker->bSharesAbilityCooldowns));
		ASSERT_THAT(AreEqual(2, Broker->Members.Num()));
		const FSeinEntityHandle First = Broker->Members[0];
		const FSeinEntityHandle Second = Broker->Members[1];
		TArray<int32> IDs;
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			for (const FSeinEntityHandle Member : {First, Second})
			{
				World->AddComponent(Member, FSeinAbilityPayload());
				const int32 ID = USeinAbilityBPFL::SeinGrantAbility(World, Member, USeinVehicleGymAbility::StaticClass());
				IDs.Add(ID);
				USeinAbility* Ability = World->GetAbilityInstance(ID);
				ASSERT_THAT(IsNotNull(Ability));
				Ability->AbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
				Ability->Cooldown = FFixedPoint::FromInt(2);
				Ability->bRefundCooldownOnCancel = true;
			}
			ASSERT_THAT(IsTrue(World->GetAbilityInstance(IDs[0])->ActivateAbility({}, {})));
			ASSERT_THAT(IsTrue(World->GetAbilityInstance(IDs[1])->IsOnCooldown()));
			const FSeinSquadPayload* Data = World->GetComponent<FSeinSquadPayload>(Squad);
			const int32 SecondSlot = Data->Slots.IndexOfByPredicate([Second](const FSeinSquadSlot& Slot)
			{
				return Slot.CurrentOccupant == Second;
			});
			ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::SeinEmptySquadSlotByIndex(World, Squad, SecondSlot)));
			const FSeinEntityHandle Replacement = World->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID::Neutral());
			ASSERT_THAT(IsTrue(USeinSquadMutationBPFL::SeinFillSquadSlotByIndex(World, Squad, SecondSlot, Replacement)));
			World->AddComponent(Replacement, FSeinAbilityPayload());
			const int32 ReplacementID = USeinAbilityBPFL::SeinGrantAbility(World, Replacement, USeinVehicleGymAbility::StaticClass());
			USeinAbility* ReplacementAbility = World->GetAbilityInstance(ReplacementID);
			ASSERT_THAT(IsNotNull(ReplacementAbility));
			ReplacementAbility->AbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
			ReplacementAbility->Cooldown = FFixedPoint::FromInt(2);
			ReplacementAbility->CooldownScope = ESeinCooldownScope::OwnerOnly;
			ASSERT_THAT(IsTrue(ReplacementAbility->ActivateAbility({}, {})));
			World->GetAbilityInstance(IDs[0])->CancelAbility();
			ASSERT_THAT(IsFalse(World->GetAbilityInstance(IDs[1])->IsOnCooldown()));
			ASSERT_THAT(IsTrue(ReplacementAbility->IsOnCooldown()));
			// Existing/restored brokers also have their sharing policy normalized.
			World->GetComponentMutable<FSeinCommandBrokerData>(Squad)->bSharesAbilityCooldowns = false;
		}
		FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds());
		World->StopSimulation();
		ASSERT_THAT(IsTrue(World->GetComponent<FSeinCommandBrokerData>(Squad)->bSharesAbilityCooldowns));
	}
}
