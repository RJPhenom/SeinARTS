#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Components/SeinBrokerMembershipData.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinCommandBrokerData.h"
#include "Containers/Ticker.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinBrokerCallbackSafetyTestTypes.h"

FSeinBrokerDispatchPlan USeinBrokerCallbackSafetyResolver::ResolveDispatch_Implementation(
	USeinWorldSubsystem* World,
	FSeinEntityHandle BrokerHandle,
	const FSeinBrokerOrderInput& Order)
{
	++ResolveCalls;
	ObservedTargets.Add(Order.TargetLocation);

	const bool bGrowAndMutate = bMutateOrderOnNextResolve;
	const bool bGrowAndDestroy = bDestroyBrokerOnNextResolve;
	const bool bReturnNonMember = bReturnNonMemberOnNextResolve;
	const bool bReturnDuplicate = bReturnDuplicateOnNextResolve;
	const bool bReturnOversizedTargeterPoints =
		bReturnOversizedTargeterPointsOnNextResolve;
	const bool bReturnBrokerCarrier = bReturnBrokerCarrierOnNextResolve;
	const bool bNestedCommit = bCommitNestedOutputAndStaleOuter;
	bMutateOrderOnNextResolve = false;
	bDestroyBrokerOnNextResolve = false;
	bReturnNonMemberOnNextResolve = false;
	bReturnDuplicateOnNextResolve = false;
	bReturnOversizedTargeterPointsOnNextResolve = false;
	bReturnBrokerCarrierOnNextResolve = false;
	bCommitNestedOutputAndStaleOuter = false;

	if (World && (bGrowAndMutate || bGrowAndDestroy))
	{
		// The fixture enters with the pool exactly full. The first acquire grows
		// the entity pool; adding the same component type at those new high slots
		// also grows/reallocates the broker component storage.
		for (int32 Index = 0; Index < GrowthCount; ++Index)
		{
			const FSeinEntityHandle NewEntity = World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			if (!NewEntity.IsValid()) break;
			FSeinCommandBrokerData InertBroker;
			InertBroker.bSelfCullOnEmpty = false;
			World->AddComponent(NewEntity, InertBroker);
		}

		if (bGrowAndDestroy)
		{
			World->DestroyEntity(BrokerHandle);
		}
		else if (FSeinCommandBrokerData* Broker =
			World->GetComponent<FSeinCommandBrokerData>(BrokerHandle))
		{
			if (!Broker->OrderQueue.IsEmpty())
			{
				Broker->OrderQueue[0].TargetLocation = ReplacementTarget;
			}
		}
	}

	if (World && bNestedCommit && World->IsEntityAlive(BrokerHandle))
	{
		if (FSeinCommandBrokerData* Broker =
			World->GetComponent<FSeinCommandBrokerData>(BrokerHandle))
		{
			// Simulates a native same-broker nested dispatch that commits layout B
			// and advances an input token while outer plan A is still resolving.
			Broker->AnchorFacing = NestedFacing;
			Broker->SettledSlotPositions = {NestedSettledPosition};
			Broker->SettledSlotFacings = {NestedFacing};
			Broker->bSettledSlotsMemberAligned = true;
			if (!Broker->OrderQueue.IsEmpty())
			{
				Broker->OrderQueue[0].TargetLocation = ReplacementTarget;
			}
		}
	}

	FSeinBrokerDispatchPlan Plan;
	Plan.bApplyAnchorFacing = true;
	Plan.AnchorFacing = FFixedQuaternion(
		FFixedPoint::Zero, FFixedPoint::Zero,
		FFixedPoint::One, FFixedPoint::Zero);
	Plan.bApplySettledSlots = true;
	Plan.SettledSlotPositions = {Order.TargetLocation};
	Plan.SettledSlotFacings = {Plan.AnchorFacing};
	Plan.bSettledSlotsMemberAligned = true;
	if (!Order.EffectiveMembers.IsEmpty())
	{
		FSeinBrokerMemberDispatch& Dispatch =
			Plan.MemberDispatches.AddDefaulted_GetRef();
		Dispatch.Member = bReturnBrokerCarrier
			? BrokerHandle
			: (bReturnNonMember ? InjectedMember : Order.EffectiveMembers[0]);
		Dispatch.AbilityTag = Order.PredeterminedAbilityTag;
		Dispatch.TargetEntity = Order.TargetEntity;
		Dispatch.TargetLocation = Order.TargetLocation;
		if (bReturnOversizedTargeterPoints)
		{
			// Built-in ActivateAbility.V1 freezes this bound at 256.
			Dispatch.TargeterPoints.SetNum(257);
		}
		if (bReturnDuplicate)
		{
			// TArray deliberately rejects adding from one of its own element
			// references. Copy first so this malformed resolver plan reaches the
			// broker's duplicate-dispatch validation instead of crashing here.
			const FSeinBrokerMemberDispatch DuplicateDispatch = Dispatch;
			Plan.MemberDispatches.Add(DuplicateDispatch);
		}
	}
	return Plan;
}

namespace
{
	void TickOnce(USeinWorldSubsystem& World)
	{
		if (!SeinTestMatchBootstrap::Start(World)) return;
		FTSTicker::GetCoreTicker().Tick(World.GetFixedDeltaTimeSeconds());
		World.StopSimulation();
	}

	struct FBrokerCallbackFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = nullptr;
		FSeinEntityHandle Member;
		FSeinEntityHandle Outsider;
		FSeinEntityHandle Broker;
		USeinBrokerCallbackSafetyResolver* Resolver = nullptr;
		FFixedVector InitialTarget = FFixedVector(
			FFixedPoint::FromInt(100), FFixedPoint::Zero, FFixedPoint::Zero);
		int32 CapacityBeforeCallback = 0;

		FBrokerCallbackFixture()
		{
			World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
			check(World);
			const FSeinPlayerID Player(1);
			const FSeinPlayerID Enemy(2);
			const auto AuthorState = [this, Player, Enemy]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));
				World->RegisterPlayer(Enemy, FSeinFactionID(2));

				Member = World->SpawnAbstractEntity(FFixedTransform(), Player);
				Outsider = World->SpawnAbstractEntity(FFixedTransform(), Enemy);
				Broker = World->SpawnAbstractEntity(FFixedTransform(), Player);
				check(Member.IsValid() && Outsider.IsValid() && Broker.IsValid());

				auto GrantTestAbility = [this](FSeinEntityHandle Entity)
				{
					World->AddComponent(Entity, FSeinAbilityComponent());
					const int32 AbilityID = USeinAbilityBPFL::SeinGrantAbility(
						World, Entity,
						USeinBrokerCallbackSafetyAbility::StaticClass());
					USeinAbility* Ability = World->GetAbilityInstance(AbilityID);
					check(Ability);
					Ability->AbilityTag =
						SeinARTSTags::Command_Context_AbilityTriggered;
				};
				GrantTestAbility(Member);
				GrantTestAbility(Outsider);
				GrantTestAbility(Broker);

				Resolver = NewObject<USeinBrokerCallbackSafetyResolver>(World);
				check(Resolver);
				Resolver->InjectedMember = Outsider;
				const int32 ResolverID =
					World->RegisterCommandBrokerResolver(Resolver);
				check(ResolverID != INDEX_NONE);

				FSeinBrokerMembershipData Membership;
				Membership.CurrentBrokerHandle = Broker;
				World->AddComponent(Member, Membership);

				FSeinBrokerQueuedOrder Order;
				Order.TargetMembers.Add(Member);
				Order.TargetLocation = InitialTarget;
				Order.PredeterminedAbilityTag =
					SeinARTSTags::Command_Context_AbilityTriggered;
				FSeinCommandBrokerData BrokerData;
				BrokerData.Members.Add(Member);
				BrokerData.ResolverID = ResolverID;
				BrokerData.bSelfCullOnEmpty = false;
				BrokerData.OrderQueue.Add(Order);
				World->AddComponent(Broker, BrokerData);

				CapacityBeforeCallback = World->GetEntityPool().GetCapacity();
				while (World->GetEntityPool().GetActiveCount()
					< CapacityBeforeCallback)
				{
					check(World->SpawnAbstractEntity(
						FFixedTransform(), Player).IsValid());
				}
			};
			check(SeinTestMatchBootstrap::Materialize(*World, AuthorState));
		}
	};

	template <typename TTestRunner>
	void ExpectAbilityHashDiagnostic(TTestRunner& TestRunner)
	{
		TestRunner.AddExpectedError(
			TEXT("Component 'SeinAbilityComponent' has field(s) excluded from the legacy local state fingerprint"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
	}
}

namespace UE::SeinARTSTests
{
	TEST(ResolverStorageGrowthAndOrderMutationDiscardTheStalePlan,
		"SeinARTS.Sim.Broker.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FBrokerCallbackFixture Fixture;
		ASSERT_THAT(IsNotNull(Fixture.World));
		ASSERT_THAT(IsNotNull(Fixture.Resolver));

		const FFixedVector Replacement(
			FFixedPoint::FromInt(900), FFixedPoint::FromInt(25), FFixedPoint::Zero);
		Fixture.Resolver->ReplacementTarget = Replacement;
		Fixture.Resolver->bMutateOrderOnNextResolve = true;

		TickOnce(*Fixture.World);
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(1, Broker->OrderQueue.Num()));
		ASSERT_THAT(IsFalse(Broker->OrderQueue[0].bIsExecuting));
		ASSERT_THAT(IsTrue(Broker->OrderQueue[0].TargetLocation == Replacement));
		ASSERT_THAT(AreEqual(0, Broker->SettledSlotPositions.Num()));
		ASSERT_THAT(IsFalse(Broker->bSettledSlotsMemberAligned));
		ASSERT_THAT(AreEqual(1, Fixture.Resolver->ResolveCalls));
		ASSERT_THAT(AreEqual(1, Fixture.Resolver->ObservedTargets.Num()));
		ASSERT_THAT(IsTrue(
			Fixture.Resolver->ObservedTargets[0] == Fixture.InitialTarget));
		ASSERT_THAT(IsTrue(Fixture.World->GetEntityPool().GetCapacity()
			> Fixture.CapacityBeforeCallback));

		const ISeinComponentStorage* Storage =
			Fixture.World->GetComponentStorageRaw(
				FSeinCommandBrokerData::StaticStruct());
		ASSERT_THAT(IsNotNull(Storage));
		ASSERT_THAT(AreEqual(
			1 + Fixture.Resolver->GrowthCount, Storage->GetComponentCount()));

		TickOnce(*Fixture.World);
		Broker = Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(1, Broker->OrderQueue.Num()));
		ASSERT_THAT(IsTrue(Broker->OrderQueue[0].bIsExecuting));
		ASSERT_THAT(IsTrue(Broker->Anchor == Replacement));
		ASSERT_THAT(AreEqual(1, Broker->SettledSlotPositions.Num()));
		ASSERT_THAT(IsTrue(Broker->SettledSlotPositions[0] == Replacement));
		ASSERT_THAT(IsTrue(Broker->bSettledSlotsMemberAligned));
		ASSERT_THAT(AreEqual(2, Fixture.Resolver->ResolveCalls));
		ASSERT_THAT(AreEqual(2, Fixture.Resolver->ObservedTargets.Num()));
		ASSERT_THAT(IsTrue(Fixture.Resolver->ObservedTargets[1] == Replacement));
	}

	TEST(ResolverStorageGrowthAndBrokerDestructionCannotCommit,
		"SeinARTS.Sim.Broker.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FBrokerCallbackFixture Fixture;
		Fixture.Resolver->bDestroyBrokerOnNextResolve = true;
		bool bSawDestroyNotification = false;
		bool bSawUncommittedBrokerState = false;
		const FDelegateHandle DestroyCallback =
			Fixture.World->OnEntityDestroyed.AddLambda(
				[&](FSeinEntityHandle Destroyed)
				{
					if (Destroyed != Fixture.Broker) return;
					bSawDestroyNotification = true;
					const FSeinCommandBrokerData* DyingBroker =
						Fixture.World->GetDestroyingComponent<
							FSeinCommandBrokerData>(Destroyed);
					bSawUncommittedBrokerState = DyingBroker
						&& DyingBroker->OrderQueue.Num() == 1
						&& !DyingBroker->OrderQueue[0].bIsExecuting
						&& DyingBroker->SettledSlotPositions.IsEmpty();
				});

		TickOnce(*Fixture.World);
		ASSERT_THAT(IsFalse(Fixture.World->IsEntityAlive(Fixture.Broker)));
		ASSERT_THAT(AreEqual(1, Fixture.Resolver->ResolveCalls));
		ASSERT_THAT(IsNull(
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker)));
		ASSERT_THAT(IsFalse(bSawDestroyNotification));
		ASSERT_THAT(IsTrue(Fixture.World->GetEntityPool().GetCapacity()
			> Fixture.CapacityBeforeCallback));

		// The resolver killed the broker after this tick's teardown pass. Observe
		// its unchanged state only through the exact next-tick destroy window.
		TickOnce(*Fixture.World);
		Fixture.World->OnEntityDestroyed.Remove(DestroyCallback);
		ASSERT_THAT(IsTrue(bSawDestroyNotification));
		ASSERT_THAT(IsTrue(bSawUncommittedBrokerState));
		ASSERT_THAT(IsNull(
			Fixture.World->GetDestroyingComponent<FSeinCommandBrokerData>(
				Fixture.Broker)));
	}

	TEST(ResolverCannotDispatchAnEnemyOutsideTheEffectiveRoster,
		"SeinARTS.Sim.Broker.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FBrokerCallbackFixture Fixture;
		Fixture.Resolver->bReturnNonMemberOnNextResolve = true;

		TickOnce(*Fixture.World);
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(AreEqual(1, Broker->OrderQueue.Num()));
		ASSERT_THAT(IsFalse(Broker->OrderQueue[0].bIsExecuting));
		ASSERT_THAT(AreEqual(0, Broker->SettledSlotPositions.Num()));
	}

	TEST(ResolverDuplicateDispatcherRejectsTheWholePlan,
		"SeinARTS.Sim.Broker.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FBrokerCallbackFixture Fixture;
		Fixture.Resolver->bReturnDuplicateOnNextResolve = true;

		TickOnce(*Fixture.World);
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsFalse(Broker->OrderQueue[0].bIsExecuting));
		ASSERT_THAT(AreEqual(0, Broker->SettledSlotPositions.Num()));
	}

	TEST(ResolverSchemaViolationRejectsTheWholePlanBeforeExecution,
		"SeinARTS.Sim.Broker.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FBrokerCallbackFixture Fixture;
		Fixture.Resolver->bReturnOversizedTargeterPointsOnNextResolve = true;

		TickOnce(*Fixture.World);
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsFalse(Broker->OrderQueue[0].bIsExecuting));
		ASSERT_THAT(AreEqual(0, Broker->SettledSlotPositions.Num()));
	}

	TEST(BrokerCarrierWithTheActualAbilityIsAnAuthorizedDispatcher,
		"SeinARTS.Sim.Broker.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FBrokerCallbackFixture Fixture;
		Fixture.Resolver->bReturnBrokerCarrierOnNextResolve = true;

		TickOnce(*Fixture.World);
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsTrue(Broker->OrderQueue[0].bIsExecuting));
		ASSERT_THAT(AreEqual(1, Broker->SettledSlotPositions.Num()));
	}

	TEST(NestedCommittedLayoutSurvivesAStaleOuterPlan,
		"SeinARTS.Sim.Broker.CallbackSafety")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FBrokerCallbackFixture Fixture;
		const FFixedVector NestedPosition(
			FFixedPoint::FromInt(777), FFixedPoint::FromInt(-40),
			FFixedPoint::Zero);
		const FFixedVector ReplacementTarget(
			FFixedPoint::FromInt(930), FFixedPoint::FromInt(20),
			FFixedPoint::Zero);
		Fixture.Resolver->NestedSettledPosition = NestedPosition;
		Fixture.Resolver->NestedFacing = FFixedQuaternion(
			FFixedPoint::Zero, FFixedPoint::One,
			FFixedPoint::Zero, FFixedPoint::Zero);
		Fixture.Resolver->ReplacementTarget = ReplacementTarget;
		Fixture.Resolver->bCommitNestedOutputAndStaleOuter = true;

		TickOnce(*Fixture.World);
		const FSeinCommandBrokerData* Broker =
			Fixture.World->GetComponent<FSeinCommandBrokerData>(Fixture.Broker);
		ASSERT_THAT(IsNotNull(Broker));
		ASSERT_THAT(IsFalse(Broker->OrderQueue[0].bIsExecuting));
		ASSERT_THAT(IsTrue(
			Broker->OrderQueue[0].TargetLocation == ReplacementTarget));
		ASSERT_THAT(IsTrue(Broker->AnchorFacing
			== Fixture.Resolver->NestedFacing));
		ASSERT_THAT(AreEqual(1, Broker->SettledSlotPositions.Num()));
		ASSERT_THAT(IsTrue(
			Broker->SettledSlotPositions[0] == NestedPosition));
		ASSERT_THAT(IsTrue(Broker->bSettledSlotsMemberAligned));
	}
}
