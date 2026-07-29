#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Containers/Ticker.h"
#include "Components/SeinContainmentData.h"
#include "Components/SeinContainmentMemberData.h"
#include "Components/SeinExtentsComponent.h"
#include "Simulation/SeinTestSimContext.h"
#include "Lib/SeinRandomBPFL.h"
#include "Lib/SeinResourceBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"

namespace UE::SeinARTSTests
{
	TEST(StateMutationGateCoversApplyingSealedAndSimulationContexts,
		"SeinARTS.Unit.CoreEntity.MutationAuthorization")
	{
		FActorTestSpawner Spawner;
		UWorld& TestWorld = Spawner.GetWorld();
		USeinWorldSubsystem* World =
			TestWorld.GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle ApplyingEntity;
		FSeinEntityHandle Member;
		FSeinEntityHandle Container;
		FSeinResourceCost Income;
		Income.Amounts.Add(
			SeinARTSTags::Resource, FFixedPoint::FromInt(5));

		bool bEnteredDuringApplying = false;
		bool bExitedDuringApplying = false;
		int64 ApplyingBalanceBefore = 0;
		int64 ApplyingBalanceAfter = 0;
		uint64 ApplyingRandomBefore0 = 0;
		uint64 ApplyingRandomBefore1 = 0;
		uint64 ApplyingRandomAfter0 = 0;
		uint64 ApplyingRandomAfter1 = 0;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			ApplyingEntity = World->SpawnAbstractEntity(
				FFixedTransform(), Player);
			Member = World->SpawnAbstractEntity(FFixedTransform(), Player);
			Container = World->SpawnAbstractEntity(FFixedTransform(), Player);

			World->AddComponent(ApplyingEntity, FSeinExtentsComponent());
			World->AddComponent(Member, FSeinContainmentMemberData());
			World->AddComponent(Container, FSeinContainmentData());

			ApplyingBalanceBefore = USeinResourceBPFL::SeinGetResource(
				&TestWorld, Player, SeinARTSTags::Resource).Value;
			USeinResourceBPFL::SeinGrantIncome(&TestWorld, Player, Income);
			ApplyingBalanceAfter = USeinResourceBPFL::SeinGetResource(
				&TestWorld, Player, SeinARTSTags::Resource).Value;

			World->SimRandom.GetState(
				ApplyingRandomBefore0, ApplyingRandomBefore1);
			USeinRandomBPFL::SeinRandomIntRange(&TestWorld, 17, 91);
			World->SimRandom.GetState(
				ApplyingRandomAfter0, ApplyingRandomAfter1);

			bEnteredDuringApplying = World->EnterContainer(Member, Container);
			bExitedDuringApplying = World->ExitContainer(Member);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			12345,
			TEXT("SeinARTS.MutationAuthorization"))));

		ASSERT_THAT(IsNotNull(
			World->GetComponent<FSeinExtentsComponent>(ApplyingEntity)));
		ASSERT_THAT(IsTrue(ApplyingBalanceAfter > ApplyingBalanceBefore));
		ASSERT_THAT(IsTrue(
			ApplyingRandomBefore0 != ApplyingRandomAfter0
			|| ApplyingRandomBefore1 != ApplyingRandomAfter1));
		ASSERT_THAT(IsTrue(bEnteredDuringApplying));
		ASSERT_THAT(IsTrue(bExitedDuringApplying));
		ASSERT_THAT(IsFalse(World->IsContained(Member)));

		const int64 SealedBalance = USeinResourceBPFL::SeinGetResource(
			&TestWorld, Player, SeinARTSTags::Resource).Value;
		uint64 SealedRandomBefore0 = 0;
		uint64 SealedRandomBefore1 = 0;
		World->SimRandom.GetState(SealedRandomBefore0, SealedRandomBefore1);
		TestRunner->AddExpectedError(
			TEXT("AddComponent rejected outside bootstrap Applying"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("GrantIncome rejected outside bootstrap Applying"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("RandomIntRange rejected outside bootstrap Applying"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("EnterContainer rejected outside bootstrap Applying"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		World->AddComponent(Member, FSeinExtentsComponent());
		USeinResourceBPFL::SeinGrantIncome(&TestWorld, Player, Income);
		USeinRandomBPFL::SeinRandomIntRange(&TestWorld, 17, 91);
		ASSERT_THAT(IsFalse(World->EnterContainer(Member, Container)));

		uint64 SealedRandomAfter0 = 0;
		uint64 SealedRandomAfter1 = 0;
		World->SimRandom.GetState(SealedRandomAfter0, SealedRandomAfter1);
		ASSERT_THAT(IsNull(World->GetComponent<FSeinExtentsComponent>(Member)));
		ASSERT_THAT(AreEqual(
			SealedBalance,
			USeinResourceBPFL::SeinGetResource(
				&TestWorld, Player, SeinARTSTags::Resource).Value));
		ASSERT_THAT(AreEqual(SealedRandomBefore0, SealedRandomAfter0));
		ASSERT_THAT(AreEqual(SealedRandomBefore1, SealedRandomAfter1));
		ASSERT_THAT(IsFalse(World->IsContained(Member)));

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));
		int64 SimBalanceBefore = USeinResourceBPFL::SeinGetResource(
			&TestWorld, Player, SeinARTSTags::Resource).Value;
		uint64 SimRandomBefore0 = 0;
		uint64 SimRandomBefore1 = 0;
		World->SimRandom.GetState(SimRandomBefore0, SimRandomBefore1);
		bool bEnteredDuringSim = false;
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->AddComponent(Member, FSeinExtentsComponent());
			USeinResourceBPFL::SeinGrantIncome(&TestWorld, Player, Income);
			USeinRandomBPFL::SeinRandomIntRange(&TestWorld, 17, 91);
			bEnteredDuringSim = World->EnterContainer(Member, Container);
		}
		uint64 SimRandomAfter0 = 0;
		uint64 SimRandomAfter1 = 0;
		World->SimRandom.GetState(SimRandomAfter0, SimRandomAfter1);
		ASSERT_THAT(IsNotNull(
			World->GetComponent<FSeinExtentsComponent>(Member)));
		ASSERT_THAT(IsTrue(
			USeinResourceBPFL::SeinGetResource(
				&TestWorld, Player, SeinARTSTags::Resource).Value
			> SimBalanceBefore));
		ASSERT_THAT(IsTrue(
			SimRandomBefore0 != SimRandomAfter0
			|| SimRandomBefore1 != SimRandomAfter1));
		ASSERT_THAT(IsTrue(bEnteredDuringSim));
		ASSERT_THAT(IsTrue(World->IsContained(Member)));
		World->StopSimulation();
	}

	TEST(EntityOwnerMutationIsAuthorizedDuringBootstrapApplying,
		"SeinARTS.Unit.CoreEntity.MutationAuthorization")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID InitialOwner(1);
		const FSeinPlayerID NewOwner(2);
		FSeinEntityHandle Entity;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(InitialOwner, FSeinFactionID(1));
			World->RegisterPlayer(NewOwner, FSeinFactionID(2));
			Entity = World->SpawnAbstractEntity(
				FFixedTransform(), InitialOwner);
			World->SetEntityOwner(Entity, NewOwner);
		};

		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			AuthorState,
			FSeinMatchSettings(),
			0,
			TEXT("SeinARTS.EntityOwner.BootstrapApplying"))));
		ASSERT_THAT(IsTrue(World->IsEntityAlive(Entity)));
		ASSERT_THAT(IsTrue(World->GetEntityOwner(Entity) == NewOwner));
	}

	TEST(StateMutationContextIsBoundToTheExactWorld,
		"SeinARTS.Unit.CoreEntity.MutationAuthorization")
	{
		FActorTestSpawner FirstSpawner;
		FActorTestSpawner SecondSpawner;
		USeinWorldSubsystem* First =
			FirstSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		USeinWorldSubsystem* Second =
			SecondSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(First));
		ASSERT_THAT(IsNotNull(Second));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*First, FSeinMatchSettings(), 0, TEXT("MutationWorldA"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*Second, FSeinMatchSettings(), 0, TEXT("MutationWorldB"))));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*First)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Second)));

		FSeinEntityHandle FirstEntity;
		FSeinEntityHandle CrossWorldEntity;
		TestRunner->AddExpectedError(
			TEXT("SpawnAbstractEntity rejected outside bootstrap Applying or deterministic simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		{
			auto FirstWorldScope = FSeinSimContextTestAccess::Enter(*First);
			FirstEntity = First->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			CrossWorldEntity = Second->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
		}

		ASSERT_THAT(IsTrue(FirstEntity.IsValid()));
		ASSERT_THAT(IsFalse(CrossWorldEntity.IsValid()));
		First->StopSimulation();
		Second->StopSimulation();
	}

	TEST(DeferredContainerDeathEjectsThroughThePrivateTeardownPath,
		"SeinARTS.Unit.CoreEntity.MutationAuthorization")
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FFixedVector ContainerLocation(
			FFixedPoint::FromInt(13),
			FFixedPoint::FromInt(-7),
			FFixedPoint::FromInt(2));
		FSeinEntityHandle Member;
		FSeinEntityHandle Container;
		bool bEntered = false;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				Member = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				FFixedTransform ContainerTransform;
				ContainerTransform.SetLocation(ContainerLocation);
				Container = World->SpawnAbstractEntity(
					ContainerTransform, FSeinPlayerID::Neutral());
				World->AddComponent(
					Member, FSeinContainmentMemberData());
				World->AddComponent(
					Container, FSeinContainmentData());
				bEntered = World->EnterContainer(Member, Container);
			},
			FSeinMatchSettings(), 0,
			TEXT("DeferredContainerDeath"))));
		ASSERT_THAT(IsTrue(bEntered));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->DestroyEntity(Container);
		}
		FTSTicker::GetCoreTicker().Tick(
			World->GetFixedDeltaTimeSeconds());

		ASSERT_THAT(IsFalse(World->IsEntityAlive(Container)));
		ASSERT_THAT(IsTrue(World->IsEntityAlive(Member)));
		ASSERT_THAT(IsFalse(World->IsContained(Member)));
		const FSeinEntity* MemberEntity = World->GetEntity(Member);
		ASSERT_THAT(IsNotNull(MemberEntity));
		ASSERT_THAT(IsTrue(
			MemberEntity->Transform.GetLocation() == ContainerLocation));
		World->StopSimulation();
	}
}
