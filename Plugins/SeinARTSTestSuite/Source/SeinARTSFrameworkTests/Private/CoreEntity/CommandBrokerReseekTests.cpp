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

	int32 GetReseekJitterWindow(const USeinARTSCoreSettings& Settings)
	{
		const int32 TickRate = Settings.SimulationTickRate > 1
			? Settings.SimulationTickRate
			: 1;
		const int32 JitterWindow = (TickRate * 3) / 2;
		return JitterWindow > 1 ? JitterWindow : 1;
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
			int32 InitialEpisodeStartTick = 0,
			int32 RequiredJitter = INDEX_NONE)
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
				const int32 WindowTicks = GetReseekJitterWindow(
					*GetDefault<USeinARTSCoreSettings>());
				do
				{
					Member = World->SpawnAbstractEntity(
						FFixedTransform(DisplacedPosition), Player);
				}
				while (RequiredJitter != INDEX_NONE
					&& static_cast<int32>(GetTypeHash(Member)
						% static_cast<uint32>(WindowTicks))
						!= RequiredJitter);
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
		constexpr int32 ExpectedJitter = 3;
		ASSERT_THAT(IsTrue(Fixture.Initialize(
			false, 0, ExpectedJitter)));

		ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsNull(FindInternalReturn(*Broker, Fixture.Member)));
		const int32 EpisodeStartTick = Broker->ReseekEpisodeStartTick;
		ASSERT_THAT(AreEqual(
			Fixture.World->GetCurrentTick(), EpisodeStartTick));
		const int32 ExpectedReleaseTick = EpisodeStartTick + ExpectedJitter;

		while (Fixture.World->GetCurrentTick() + 1 < ExpectedReleaseTick)
		{
			ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
			Broker =
				Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
			ASSERT_THAT(IsNotNull(Broker));
			ASSERT_THAT(IsNull(FindInternalReturn(*Broker, Fixture.Member)));
		}
		ASSERT_THAT(AreEqual(
			ExpectedReleaseTick - 1, Fixture.World->GetCurrentTick()));

		ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		Broker = Fixture.World->GetComponent<FSeinCommandBrokerData>(
			Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(
			ExpectedReleaseTick, Fixture.World->GetCurrentTick()));
		const FSeinBrokerQueuedOrder* ReturnOrder =
			FindInternalReturn(*Broker, Fixture.Member);

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

	TEST(IdleReseekWatchCadenceAdvancesOnExactBoundary,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		Settings.Settings->ReseekWatchInterval = FFixedPoint::One;
		FBrokeredReseekFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true)));

		ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		const int32 FirstScanTick = Fixture.World->GetCurrentTick();
		const int32 WatchTicks = Settings.Settings->SimulationTickRate;
		const int32 FirstBoundary = FirstScanTick + WatchTicks;
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(FirstBoundary, Broker->NextReseekAllowedTick));

		while (Fixture.World->GetCurrentTick() + 1 < FirstBoundary)
		{
			ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
			Broker = Fixture.World->GetComponent<FSeinCommandBrokerData>(
				Fixture.Broker);
			ASSERT_THAT(IsNotNull(Broker));
			ASSERT_THAT(AreEqual(
				FirstBoundary, Broker->NextReseekAllowedTick));
		}

		ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		Broker = Fixture.World->GetComponent<FSeinCommandBrokerData>(
			Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(FirstBoundary, Fixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(
			FirstBoundary + WatchTicks, Broker->NextReseekAllowedTick));
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

	TEST(FreeReseekPairingRematchesUnreleasedMembersAroundClaimedSlot,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle ClaimedMember;
		FSeinEntityHandle UpperMember;
		FSeinEntityHandle LowerMember;
		FSeinEntityHandle BrokerHandle;
		const FFixedVector FirstSlot = FFixedVector::ZeroVector;
		const FFixedVector SecondSlot(
			FFixedPoint::FromInt(1000), FFixedPoint::Zero,
			FFixedPoint::Zero);
		const FFixedVector ThirdSlot(
			FFixedPoint::FromInt(2000), FFixedPoint::Zero,
			FFixedPoint::Zero);
		const FFixedVector UpperPosition(
			FFixedPoint::FromInt(1700), FFixedPoint::Zero,
			FFixedPoint::Zero);
		const FFixedVector LowerPosition(
			FFixedPoint::FromInt(1300), FFixedPoint::Zero,
			FFixedPoint::Zero);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			ClaimedMember = World->SpawnAbstractEntity(
				FFixedTransform(FirstSlot), Player);
			const int32 WindowTicks = GetReseekJitterWindow(
				*Settings.Settings);
			do
			{
				UpperMember = World->SpawnAbstractEntity(
					FFixedTransform(UpperPosition), Player);
			}
			while (GetTypeHash(UpperMember)
				% static_cast<uint32>(WindowTicks) != 0);
			do
			{
				LowerMember = World->SpawnAbstractEntity(
					FFixedTransform(LowerPosition), Player);
			}
			while (GetTypeHash(LowerMember)
				% static_cast<uint32>(WindowTicks) != 0);
			BrokerHandle = World->SpawnAbstractEntity(
				FFixedTransform(), Player);

			for (const TPair<FSeinEntityHandle, FFixedVector>& Entry : {
				TPair<FSeinEntityHandle, FFixedVector>(ClaimedMember, FirstSlot),
				TPair<FSeinEntityHandle, FFixedVector>(UpperMember, UpperPosition),
				TPair<FSeinEntityHandle, FFixedVector>(LowerMember, LowerPosition)})
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
			Broker.Members = {ClaimedMember, UpperMember, LowerMember};
			Broker.SettledSlotPositions = {FirstSlot, SecondSlot, ThirdSlot};
			Broker.bSettledSlotsMemberAligned = false;
			Broker.bSelfCullOnEmpty = false;
			FSeinBrokerQueuedOrder ClaimedOrder;
			ClaimedOrder.TargetMembers.Add(ClaimedMember);
			ClaimedOrder.PreplacedMembers.Add(ClaimedMember);
			ClaimedOrder.PreplacedPositions.Add(FirstSlot);
			ClaimedOrder.bIsInternalPrefix = true;
			ClaimedOrder.bIsExecuting = true;
			ClaimedOrder.LastDispatchTick = MAX_int32;
			Broker.OrderQueue.Add(ClaimedOrder);
			World->AddComponent(BrokerHandle, Broker);
		})));

		ASSERT_THAT(IsTrue(TickOnce(*World)));

		const FSeinCommandBrokerData* Broker =
			World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(3, Broker->OrderQueue.Num()));
		const FSeinBrokerQueuedOrder* UpperReturn =
			FindInternalReturn(*Broker, UpperMember);
		const FSeinBrokerQueuedOrder* LowerReturn =
			FindInternalReturn(*Broker, LowerMember);
		ASSERT_THAT(IsNotNull(UpperReturn));
		ASSERT_THAT(IsNotNull(LowerReturn));
		ASSERT_THAT(IsTrue(
			UpperReturn->PreplacedPositions[0] == ThirdSlot));
		ASSERT_THAT(IsTrue(
			LowerReturn->PreplacedPositions[0] == SecondSlot));
	}

	TEST(MovingTrafficBlocksReturnUntilItsCorridorClears,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		Settings.Settings->ReseekReleaseInterval = FFixedPoint::One;
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
				GetReseekJitterWindow(*Settings.Settings);
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
		const int32 ExpectedReleaseTick =
			World->GetCurrentTick() + Settings.Settings->SimulationTickRate;
		ASSERT_THAT(AreEqual(
			ExpectedReleaseTick, Broker->NextReseekAllowedTick));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinMovementComponent* TrafficMovement =
				World->GetComponentMutable<FSeinMovementComponent>(Traffic);
			ASSERT_THAT(IsNotNull(TrafficMovement));
			TrafficMovement->bHasTarget = false;
			TrafficMovement->Velocity = FFixedVector::ZeroVector;
		}
		while (World->GetCurrentTick() + 1 < ExpectedReleaseTick)
		{
			ASSERT_THAT(IsTrue(TickOnce(*World)));
			Broker = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
			ASSERT_THAT(IsNotNull(Broker));
			ASSERT_THAT(IsNull(FindInternalReturn(*Broker, Member)));
		}
		ASSERT_THAT(IsTrue(TickOnce(*World)));

		Broker = World->GetComponent<FSeinCommandBrokerData>(BrokerHandle);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(ExpectedReleaseTick, World->GetCurrentTick()));
		const FSeinBrokerQueuedOrder* ReturnOrder =
			FindInternalReturn(*Broker, Member);
		ASSERT_THAT(IsNotNull(ReturnOrder));
		ASSERT_THAT(IsTrue(ReturnOrder->PreplacedPositions[0] == Slot));
	}

	TEST(ExpiredIdleReseekEpisodeEntersExtendedQuietPeriod,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		Settings.Settings->ReseekMaxEpisodeSeconds = FFixedPoint::One;
		FBrokeredReseekFixture Fixture;
		ASSERT_THAT(IsTrue(Fixture.Initialize(true, 1)));

		const int32 CapBoundary =
			1 + Settings.Settings->SimulationTickRate;
		while (Fixture.World->GetCurrentTick() < CapBoundary)
		{
			ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		}

		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(
			Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(CapBoundary, Fixture.World->GetCurrentTick()));
		ASSERT_THAT(AreEqual(1, Broker->ReseekEpisodeStartTick));
		ASSERT_THAT(AreEqual(CapBoundary + 1, Broker->NextReseekAllowedTick));

		ASSERT_THAT(IsTrue(TickOnce(*Fixture.World)));
		Broker = Fixture.World->GetComponent<FSeinCommandBrokerData>(
			Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(0, Broker->ReseekEpisodeStartTick));
		ASSERT_THAT(AreEqual(
			Fixture.World->GetCurrentTick()
				+ Settings.Settings->SimulationTickRate * 2,
			Broker->NextReseekAllowedTick));
		ASSERT_THAT(AreEqual(1, Broker->OrderQueue.Num()));
	}

	TEST(MultipleUnbrokeredIdleMembersCreateIndependentHomeReturnBrokers,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		TArray<FSeinEntityHandle> Members;
		TArray<FFixedVector> Homes;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const FFixedVector Home(
					FFixedPoint::FromInt(Index * 100), FFixedPoint::Zero,
					FFixedPoint::Zero);
				const FFixedVector Displaced = Home + FFixedVector(
					FFixedPoint::FromInt(500), FFixedPoint::Zero,
					FFixedPoint::Zero);
				const FSeinEntityHandle Member = World->SpawnAbstractEntity(
					FFixedTransform(Displaced), Player);
				FSeinMovementComponent Movement;
				Movement.bHomeSeeded = true;
				Movement.HomePos = Home;
				World->AddComponent(Member, Movement);
				Members.Add(Member);
				Homes.Add(Home);
			}
		})));

		ASSERT_THAT(IsTrue(TickOnce(*World)));

		TSet<FSeinEntityHandle> BrokerHandles;
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			const FSeinBrokerMembershipData* Membership =
				World->GetComponent<FSeinBrokerMembershipData>(Members[Index]);
			ASSERT_THAT(IsNotNull(Membership));
			ASSERT_THAT(IsTrue(Membership->CurrentBrokerHandle.IsValid()));
			ASSERT_THAT(IsTrue(World->IsEntityAlive(
				Membership->CurrentBrokerHandle)));
			BrokerHandles.Add(Membership->CurrentBrokerHandle);

			const FSeinCommandBrokerData* Broker =
				World->GetComponent<FSeinCommandBrokerData>(
					Membership->CurrentBrokerHandle);
			ASSERT_THAT(IsNotNull(Broker));
			ASSERT_THAT(IsTrue(Broker->Anchor == Homes[Index]));
			ASSERT_THAT(AreEqual(1, Broker->Members.Num()));
			ASSERT_THAT(IsTrue(Broker->Members[0] == Members[Index]));
		}
		ASSERT_THAT(AreEqual(Members.Num(), BrokerHandles.Num()));
	}

	TEST(LooseIdleReseekUsesArrivalHysteresisFloor,
		"SeinARTS.Sim.Broker.IdleReseek")
	{
		FScopedIdleReseekSettings Settings;
		Settings.Settings->ReseekDisplacementThreshold =
			FFixedPoint::FromInt(50);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle InsideFloor;
		FSeinEntityHandle OutsideFloor;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			auto AddLooseMember = [&](int32 Displacement)
			{
				const FSeinEntityHandle Member = World->SpawnAbstractEntity(
					FFixedTransform(FFixedVector(
						FFixedPoint::FromInt(Displacement),
						FFixedPoint::Zero,
						FFixedPoint::Zero)),
					Player);
				FSeinMovementComponent Movement;
				Movement.bHomeSeeded = true;
				Movement.HomePos = FFixedVector::ZeroVector;
				World->AddComponent(Member, Movement);
				World->AddComponent(Member, FSeinNavigationComponent());
				return Member;
			};
			InsideFloor = AddLooseMember(51);
			OutsideFloor = AddLooseMember(101);
		})));

		ASSERT_THAT(IsTrue(TickOnce(*World)));
		ASSERT_THAT(IsNull(
			World->GetComponent<FSeinBrokerMembershipData>(InsideFloor)));
		const FSeinBrokerMembershipData* OutsideMembership =
			World->GetComponent<FSeinBrokerMembershipData>(OutsideFloor);
		ASSERT_THAT(IsNotNull(OutsideMembership));
		ASSERT_THAT(IsTrue(
			OutsideMembership->CurrentBrokerHandle.IsValid()));
	}
}
