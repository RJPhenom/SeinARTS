#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinCommandBrokerData.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "TestTypes/SeinResolverNestedCallbackSafetyTestTypes.h"

void USeinResolverNestedCallbackSafetyTestResolver::PostProcessPositions_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& /*Members*/,
	TArray<FFixedVector>& InOutPositions,
	FFixedVector /*TargetLocation*/)
{
	++CallbackCalls;
	if (!World) return;

	for (int32 Index = 0; Index < GrowthCount; ++Index)
	{
		const FSeinEntityHandle Entity = World->SpawnAbstractEntity(
			FFixedTransform(), FSeinPlayerID::Neutral());
		if (!Entity.IsValid()) break;

		FSeinCommandBrokerData InertBroker;
		InertBroker.bSelfCullOnEmpty = false;
		World->AddComponent(Entity, InertBroker);
	}

	if (!InOutPositions.IsEmpty())
	{
		InOutPositions[0] = CallbackPosition;
	}
}

namespace
{
	struct FResolverNestedCallbackFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		FSeinEntityHandle Member;
		FSeinEntityHandle Broker;
		USeinResolverNestedCallbackSafetyTestResolver* Resolver = nullptr;
		int32 CapacityBeforeCallback = 0;
		int32 BrokerComponentsBeforeCallback = 0;

		FResolverNestedCallbackFixture()
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			check(World);
			const FSeinPlayerID Player(1);
			const auto AuthorState = [this, Player]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));

				Member = World->SpawnAbstractEntity(FFixedTransform(), Player);
				Broker = World->SpawnAbstractEntity(FFixedTransform(), Player);
				check(Member.IsValid() && Broker.IsValid());

				FSeinCommandBrokerData BrokerData;
				BrokerData.Members.Add(Member);
				BrokerData.bSelfCullOnEmpty = false;
				World->AddComponent(Broker, BrokerData);

				Resolver =
					NewObject<USeinResolverNestedCallbackSafetyTestResolver>(World);
				check(Resolver);
				Resolver->CallbackPosition = FFixedVector(
					FFixedPoint::FromInt(320), FFixedPoint::FromInt(45),
					FFixedPoint::Zero);

				const ISeinComponentStorage* Storage =
					World->GetComponentStorageRaw(
						FSeinCommandBrokerData::StaticStruct());
				check(Storage);
				BrokerComponentsBeforeCallback = Storage->GetComponentCount();

				CapacityBeforeCallback = World->GetEntityPool().GetCapacity();
				while (World->GetEntityPool().GetActiveCount()
					< CapacityBeforeCallback)
				{
					check(World->SpawnAbstractEntity(
						FFixedTransform(), Player).IsValid());
				}
			};
			check(SeinTestMatchBootstrap::Materialize(*World, AuthorState));
			check(SeinTestMatchBootstrap::Start(*World));
		}
	};
}

namespace UE::SeinARTSTests
{
	TEST(DefaultResolverReacquiresBrokerAfterNestedFormationCallback,
		"SeinARTS.Sim.Broker.ResolverCallbackSafety")
	{
		FResolverNestedCallbackFixture Fixture;
		FSeinBrokerOrderInput Order;
		Order.EffectiveMembers.Add(Fixture.Member);
		Order.TargetLocation = FFixedVector(
			FFixedPoint::FromInt(100), FFixedPoint::Zero, FFixedPoint::Zero);

		FSeinBrokerDispatchPlan Plan;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*Fixture.World);
			Plan = Fixture.Resolver->ResolveDispatch(
				Fixture.World, Fixture.Broker, Order);
		}

		ASSERT_THAT(AreEqual(1, Fixture.Resolver->CallbackCalls));
		ASSERT_THAT(IsTrue(Fixture.World->GetEntityPool().GetCapacity()
			> Fixture.CapacityBeforeCallback));

		const ISeinComponentStorage* Storage = Fixture.World->GetComponentStorageRaw(
			FSeinCommandBrokerData::StaticStruct());
		ASSERT_THAT(IsNotNull(Storage));
		ASSERT_THAT(AreEqual(
			Fixture.BrokerComponentsBeforeCallback + Fixture.Resolver->GrowthCount,
			Storage->GetComponentCount()));

		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(0, Broker->SettledSlotPositions.Num()));
		ASSERT_THAT(AreEqual(0, Broker->SettledSlotFacings.Num()));
		ASSERT_THAT(IsFalse(Broker->bSettledSlotsMemberAligned));

		ASSERT_THAT(IsTrue(Plan.bApplyAnchorFacing));
		ASSERT_THAT(IsTrue(Plan.bApplySettledSlots));
		ASSERT_THAT(AreEqual(1, Plan.SettledSlotPositions.Num()));
		ASSERT_THAT(IsTrue(
			Plan.SettledSlotPositions[0] == Fixture.Resolver->CallbackPosition));
		ASSERT_THAT(AreEqual(1, Plan.SettledSlotFacings.Num()));
		ASSERT_THAT(IsFalse(Plan.bSettledSlotsMemberAligned));
	}
}
