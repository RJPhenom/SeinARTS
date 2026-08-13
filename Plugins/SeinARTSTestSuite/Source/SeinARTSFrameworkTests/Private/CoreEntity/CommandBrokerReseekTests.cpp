#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinCommandBrokerData.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Containers/Ticker.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace
{
	struct FScopedIdleReseekSettings
	{
		FScopedIdleReseekSettings()
		{
			Settings = GetMutableDefault<USeinARTSCoreSettings>();
			check(Settings);
			bSavedIdleReseek = Settings->bIdleReseek;
			SavedThreshold = Settings->ReseekDisplacementThreshold;
			SavedWatchInterval = Settings->ReseekWatchInterval;
			SavedReleaseInterval = Settings->ReseekReleaseInterval;
			SavedMaxEpisodeSeconds = Settings->ReseekMaxEpisodeSeconds;
			SavedResolverClass = Settings->DefaultBrokerResolverClass;

			Settings->bIdleReseek = true;
			Settings->ReseekDisplacementThreshold = FFixedPoint::FromInt(10);
			Settings->ReseekWatchInterval = FFixedPoint::Zero;
			Settings->ReseekReleaseInterval = FFixedPoint::Zero;
			Settings->ReseekMaxEpisodeSeconds = FFixedPoint::FromInt(4);
			Settings->DefaultBrokerResolverClass =
				USeinDefaultCommandBrokerResolver::StaticClass();
		}

		~FScopedIdleReseekSettings()
		{
			Settings->bIdleReseek = bSavedIdleReseek;
			Settings->ReseekDisplacementThreshold = SavedThreshold;
			Settings->ReseekWatchInterval = SavedWatchInterval;
			Settings->ReseekReleaseInterval = SavedReleaseInterval;
			Settings->ReseekMaxEpisodeSeconds = SavedMaxEpisodeSeconds;
			Settings->DefaultBrokerResolverClass = SavedResolverClass;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		bool bSavedIdleReseek = false;
		FFixedPoint SavedThreshold;
		FFixedPoint SavedWatchInterval;
		FFixedPoint SavedReleaseInterval;
		FFixedPoint SavedMaxEpisodeSeconds;
		TSoftClassPtr<USeinCommandBrokerResolver> SavedResolverClass;
	};

	bool TickOnce(USeinWorldSubsystem& World)
	{
		if (!SeinTestMatchBootstrap::Start(World))
		{
			return false;
		}
		FTSTicker::GetCoreTicker().Tick(World.GetFixedDeltaTimeSeconds());
		World.StopSimulation();
		return true;
	}

	const FSeinBrokerQueuedOrder* FindInternalReturn(
		const FSeinCommandBrokerData& Broker,
		FSeinEntityHandle Member)
	{
		for (const FSeinBrokerQueuedOrder& Order : Broker.OrderQueue)
		{
			if (Order.bIsInternalPrefix
				&& Order.TargetMembers.Num() == 1
				&& Order.TargetMembers[0] == Member)
			{
				return &Order;
			}
		}
		return nullptr;
	}

	struct FBrokeredReseekFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		FSeinEntityHandle Member;
		FSeinEntityHandle Broker;
		const FFixedVector Slot = FFixedVector::ZeroVector;
		const FFixedVector DisplacedPosition = FFixedVector(
			FFixedPoint::FromInt(500), FFixedPoint::Zero, FFixedPoint::Zero);

		bool Initialize(
			bool bWithForeignOrder = false,
			int32 InitialEpisodeStartTick = 0)
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			if (!World)
			{
				return false;
			}

			const FSeinPlayerID Player(1);
			return SeinTestMatchBootstrap::Materialize(*World, [&]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));
				Member = World->SpawnAbstractEntity(
					FFixedTransform(DisplacedPosition), Player);
				Broker = World->SpawnAbstractEntity(FFixedTransform(), Player);

				FSeinMovementComponent Movement;
				Movement.bHomeSeeded = true;
				Movement.HomePos = DisplacedPosition;
				World->AddComponent(Member, Movement);

				FSeinNavigationComponent Navigation;
				Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
				World->AddComponent(Member, Navigation);

				FSeinBrokerMembershipData Membership;
				Membership.CurrentBrokerHandle = Broker;
				World->AddComponent(Member, Membership);

				FSeinCommandBrokerData BrokerData;
				BrokerData.Members.Add(Member);
				BrokerData.Anchor = Slot;
				BrokerData.SettledSlotPositions.Add(Slot);
				BrokerData.bSettledSlotsMemberAligned = true;
				BrokerData.bSelfCullOnEmpty = false;
				BrokerData.ReseekEpisodeStartTick = InitialEpisodeStartTick;
				if (bWithForeignOrder)
				{
					FSeinBrokerQueuedOrder ForeignOrder;
					ForeignOrder.TargetMembers.Add(Member);
					ForeignOrder.bIsExecuting = true;
					ForeignOrder.LastDispatchTick = MAX_int32;
					BrokerData.OrderQueue.Add(ForeignOrder);
				}
				World->AddComponent(Broker, BrokerData);
			});
		}
	};
}

namespace UE::SeinARTSTests
{
	TEST(BrokeredIdleMemberQueuesReturnToAlignedSlot,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		FBrokeredReseekFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize()));

		const FSeinBrokerQueuedOrder* ReturnOrder = nullptr;
		for (int32 Attempt = 0; Attempt < 64 && !ReturnOrder; ++Attempt)
		{
			ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
			const FSeinCommandBrokerData* Broker =
				Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
			ASSERT_THAT(IsNotNull(Broker));
			ReturnOrder = FindInternalReturn(*Broker, Fixture.Member);
		}

		ASSERT_THAT(IsNotNull(ReturnOrder));
		ASSERT_THAT(AreEqual(1, ReturnOrder->PreplacedMembers.Num()));
		ASSERT_THAT(IsTrue(
			ReturnOrder->PreplacedMembers[0] == Fixture.Member));
		ASSERT_THAT(AreEqual(1, ReturnOrder->PreplacedPositions.Num()));
		ASSERT_THAT(IsTrue(
			ReturnOrder->PreplacedPositions[0] == Fixture.Slot));
		ASSERT_THAT(IsTrue(ReturnOrder->TargetLocation == Fixture.Slot));
		ASSERT_THAT(IsTrue(ReturnOrder->Context.HasTagExact(
			SeinARTSTags::Command_Context_RightClick)));
		ASSERT_THAT(IsTrue(ReturnOrder->Context.HasTagExact(
			SeinARTSTags::Command_Context_Target_Ground)));
	}

	TEST(ForeignOrderSuppressesIdleReseek,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		FBrokeredReseekFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true)));

		for (int32 Tick = 0; Tick < 64; ++Tick)
		{
			ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		}

		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(1, Broker->OrderQueue.Num()));
		ASSERT_THAT(IsNull(FindInternalReturn(*Broker, Fixture.Member)));
		ASSERT_THAT(AreEqual(0, Broker->ReseekEpisodeStartTick));
	}

	TEST(FreeReseekPairingKeepsSwappedMembersOnNearestSlots,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle FirstMember;
		FSeinEntityHandle SecondMember;
		FSeinEntityHandle BrokerHandle;
		const FFixedVector FirstSlot = FFixedVector::ZeroVector;
		const FFixedVector SecondSlot(
			FFixedPoint::FromInt(1000), FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			FirstMember = World->SpawnAbstractEntity(
				FFixedTransform(SecondSlot), Player);
			SecondMember = World->SpawnAbstractEntity(
				FFixedTransform(FirstSlot), Player);
			BrokerHandle = World->SpawnAbstractEntity(
				FFixedTransform(), Player);

			for (const TPair<FSeinEntityHandle, FFixedVector>& Entry : {
				TPair<FSeinEntityHandle, FFixedVector>(FirstMember, SecondSlot),
				TPair<FSeinEntityHandle, FFixedVector>(SecondMember, FirstSlot)})
			{
				FSeinMovementComponent Movement;
				Movement.bHomeSeeded = true;
				Movement.HomePos = Entry.Value;
				World->AddComponent(Entry.Key, Movement);
				FSeinNavigationComponent Navigation;
				Navigation.AcceptanceRadius = FFixedPoint::FromInt(10);
				World->AddComponent(Entry.Key, Navigation);
				FSeinBrokerMembershipData Membership;
				Membership.CurrentBrokerHandle = BrokerHandle;
				World->AddComponent(Entry.Key, Membership);
			}

			FSeinCommandBrokerData Broker;
			Broker.Members = {FirstMember, SecondMember};
			Broker.SettledSlotPositions = {FirstSlot, SecondSlot};
			Broker.bSettledSlotsMemberAligned = false;
			Broker.bSelfCullOnEmpty = false;
			World->AddComponent(BrokerHandle, Broker);
		})));

		for (int32 Tick = 0; Tick < 4; ++Tick)
		{
			ASSERT_THAT(IsTrue(TickOnce(*World)));
		}

		const FSeinCommandBrokerData* Broker =
			World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(0, Broker->OrderQueue.Num()));
		ASSERT_THAT(AreEqual(0, Broker->ReseekEpisodeStartTick));
	}

	TEST(MovingTrafficBlocksReturnUntilItsCorridorClears,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle Member;
		FSeinEntityHandle Traffic;
		FSeinEntityHandle BrokerHandle;
		const FFixedVector Slot = FFixedVector::ZeroVector;
		const FFixedVector MemberPosition(
			FFixedPoint::FromInt(500), FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			const int32 WindowTicks =
				(Settings.Settings->SimulationTickRate * 3) / 2;
			do
			{
				Member = World->SpawnAbstractEntity(
					FFixedTransform(MemberPosition), Player);
			}
			while (GetTypeHash(Member) % static_cast<uint32>(WindowTicks) != 0);

			Traffic = World->SpawnAbstractEntity(
				FFixedTransform(FFixedVector(
					FFixedPoint::FromInt(250), FFixedPoint::Zero,
					FFixedPoint::Zero)), Player);
			BrokerHandle = World->SpawnAbstractEntity(
				FFixedTransform(), Player);

			FSeinMovementComponent MemberMovement;
			MemberMovement.bHomeSeeded = true;
			MemberMovement.HomePos = MemberPosition;
			World->AddComponent(Member, MemberMovement);
			FSeinNavigationComponent MemberNavigation;
			MemberNavigation.AcceptanceRadius = FFixedPoint::FromInt(10);
			World->AddComponent(Member, MemberNavigation);
			FSeinBrokerMembershipData Membership;
			Membership.CurrentBrokerHandle = BrokerHandle;
			World->AddComponent(Member, Membership);

			FSeinMovementComponent TrafficMovement;
			TrafficMovement.bHasTarget = true;
			TrafficMovement.TargetLocation = FFixedVector(
				FFixedPoint::FromInt(1000), FFixedPoint::Zero,
				FFixedPoint::Zero);
			TrafficMovement.Velocity = FFixedVector(
				FFixedPoint::FromInt(100), FFixedPoint::Zero,
				FFixedPoint::Zero);
			World->AddComponent(Traffic, TrafficMovement);
			FSeinExtentsComponent TrafficExtents;
			TrafficExtents.Shapes.AddDefaulted();
			TrafficExtents.bCollisionEnabled = true;
			TrafficExtents.Mobility = ESeinCollisionMobility::Movable;
			TrafficExtents.Mass = FFixedPoint::FromInt(100);
			TrafficExtents.ObjectType.Channel = FName(TEXT("Default"));
			World->AddComponent(Traffic, TrafficExtents);

			FSeinCommandBrokerData Broker;
			Broker.Members.Add(Member);
			Broker.Anchor = Slot;
			Broker.SettledSlotPositions.Add(Slot);
			Broker.bSettledSlotsMemberAligned = true;
			Broker.bSelfCullOnEmpty = false;
			World->AddComponent(BrokerHandle, Broker);
		})));

		ASSERT_THAT(IsTrue(TickOnce(*World)));
		const FSeinCommandBrokerData* Broker =
			World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsNull(FindInternalReturn(*Broker, Member)));
		ASSERT_THAT(IsTrue(Broker->ReseekEpisodeStartTick != 0));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinMovementComponent* TrafficMovement =
				World->GetComponentMutable<FSeinMovementComponent>(Traffic);
			ASSERT_THAT(IsNotNull(TrafficMovement));
			TrafficMovement->bHasTarget = false;
			TrafficMovement->Velocity = FFixedVector::ZeroVector;
		}
		ASSERT_THAT(IsTrue(TickOnce(*World)));

		Broker = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
		ASSERT_THAT(IsNotNull(Broker));
		const FSeinBrokerQueuedOrder* ReturnOrder =
			FindInternalReturn(*Broker, Member);
		ASSERT_THAT(IsNotNull(ReturnOrder));
		ASSERT_THAT(IsTrue(ReturnOrder->PreplacedPositions[0] == Slot));
	}

	TEST(ExpiredIdleReseekEpisodeEntersExtendedQuietPeriod,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		Settings.Settings->ReseekMaxEpisodeSeconds =
			FFixedPoint::One
			/ FFixedPoint::FromInt(Settings.Settings->SimulationTickRate);
		FBrokeredReseekFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true, 1)));

		for (int32 Tick = 0; Tick < 6; ++Tick)
		{
			ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		}

		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(
			Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(0, Broker->ReseekEpisodeStartTick));
		ASSERT_THAT(IsTrue(
			Broker->NextReseekAllowedTick > Fixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(1, Broker->OrderQueue.Num()));
	}

	TEST(UnbrokeredIdleMemberCreatesHomeReturnBroker,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle Member;
		const FFixedVector Home = FFixedVector::ZeroVector;
		const FFixedVector Displaced(
			FFixedPoint::FromInt(500), FFixedPoint::Zero, FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Member = World->SpawnAbstractEntity(FFixedTransform(Displaced), Player);
			FSeinMovementComponent Movement;
			Movement.bHomeSeeded = true;
			Movement.HomePos = Home;
			World->AddComponent(Member, Movement);
		})));

		ASSERT_THAT(IsTrue(TickOnce(*World)));

		const FSeinBrokerMembershipData* Membership =
			World->GetComponent<FSeinBrokerMembershipData>(Member);
		ASSERT_THAT(IsNotNull(Membership));
		ASSERT_THAT(IsTrue(Membership->CurrentBrokerHandle.IsValid()));
		ASSERT_THAT(IsTrue(World->IsEntityAlive(
			Membership->CurrentBrokerHandle)));

		const FSeinCommandBrokerData* Broker =
			World->GetComponent<FSeinCommandBrokerData>(
				Membership->CurrentBrokerHandle);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsTrue(Broker->Anchor == Home));
		ASSERT_THAT(AreEqual(1, Broker->Members.Num()));
		ASSERT_THAT(IsTrue(Broker->Members[0] == Member));
	}
}
