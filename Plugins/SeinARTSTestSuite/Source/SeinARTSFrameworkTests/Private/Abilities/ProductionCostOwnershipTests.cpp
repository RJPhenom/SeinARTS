#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinEntityBridgeComponent.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinMovementPayload.h"
#include "Components/SeinActiveEffectsPayload.h"
#include "Components/SeinProduciblePayload.h"
#include "Components/SeinProductionPayload.h"
#include "Containers/Ticker.h"
#include "Simulation/SeinTestSimContext.h"
#include "Events/SeinVisualEvent.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinDeferredDestroyTestTypes.h"
#include "TestTypes/SeinProductionCostTestTypes.h"
#include "TestTypes/SeinEffectMutationTestTypes.h"

USeinDefaultMoveTestFirstAbility::USeinDefaultMoveTestFirstAbility()
{
	AbilityTag = SeinARTSTags::Command_Context_RightClick;
	TargetType = ESeinAbilityTargetType::Point;
}

USeinDefaultMoveTestSelectedAbility::USeinDefaultMoveTestSelectedAbility()
{
	AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
	TargetType = ESeinAbilityTargetType::Point;
}

namespace
{
	constexpr int32 StartingBalance = 100;
	constexpr int32 AbilityCost = 25;

	struct FScopedResourceCatalog
	{
		explicit FScopedResourceCatalog(ESeinProductionDeductionTiming Timing)
		{
			Settings = GetMutableDefault<USeinARTSCoreSettings>();
			check(Settings);
			PreviousCatalog = Settings->ResourceCatalog;

			FSeinResourceDefinition Definition;
			Definition.ResourceTag = SeinARTSTags::Resource;
			Definition.DefaultStartingValue = FFixedPoint::FromInt(StartingBalance);
			Definition.CostDirection = ESeinCostDirection::DeductFromBalance;
			Definition.SpendBehavior = ESeinResourceSpendBehavior::RejectOnInsufficient;
			Definition.ProductionDeductionTiming = Timing;
			Settings->ResourceCatalog = {Definition};
		}

		~FScopedResourceCatalog()
		{
			Settings->ResourceCatalog = MoveTemp(PreviousCatalog);
		}

		USeinARTSCoreSettings* Settings = nullptr;
		TArray<FSeinResourceDefinition> PreviousCatalog;
	};

	struct FScopedDefaultBrokerResolver
	{
		FScopedDefaultBrokerResolver()
		{
			Settings = GetMutableDefault<USeinARTSCoreSettings>();
			check(Settings);
			Previous = Settings->DefaultBrokerResolverClass;
			Settings->DefaultBrokerResolverClass =
				USeinDefaultCommandBrokerResolver::StaticClass();
		}

		~FScopedDefaultBrokerResolver()
		{
			Settings->DefaultBrokerResolverClass = Previous;
		}

		USeinARTSCoreSettings* Settings = nullptr;
		TSoftClassPtr<USeinCommandBrokerResolver> Previous;
	};

	struct FScopedProducibleClass
	{
		explicit FScopedProducibleClass(
			FFixedPoint BuildTime,
			bool bIsResearch = false,
			bool bIncludeProductionComponent = false)
		{
			TArray<const USeinEntityBridgeComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(
				ASeinProductionCostTestActor::StaticClass(), Bridges);
			check(!Bridges.IsEmpty());
			Bridge = const_cast<USeinEntityBridgeComponent*>(Bridges[0]);
			PreviousComponentData = Bridge->ComponentData;

			Bridge->ComponentData.Reset();
			FSeinProduciblePayload Producible;
			Producible.BuildTime = BuildTime;
			Producible.bIsResearch = bIsResearch;
			Bridge->ComponentData.Add(FInstancedStruct::Make(Producible));
			if (bIncludeProductionComponent)
			{
				Bridge->ComponentData.Add(
					FInstancedStruct::Make(FSeinProductionPayload()));
			}
		}

		~FScopedProducibleClass()
		{
			Bridge->ComponentData = MoveTemp(PreviousComponentData);
		}

		USeinEntityBridgeComponent* Bridge = nullptr;
		TArray<FInstancedStruct> PreviousComponentData;
	};

	template <typename TTestRunner>
	void ExpectAbilityHashDiagnostic(TTestRunner& TestRunner)
	{
		TestRunner.AddExpectedError(
			TEXT("Component 'SeinAbilityPayload' has field(s) excluded from the legacy local state fingerprint"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
	}

	void TickOnce(USeinWorldSubsystem& World)
	{
		if (!SeinTestMatchBootstrap::Start(World)) return;
		FTSTicker::GetCoreTicker().Tick(World.GetFixedDeltaTimeSeconds());
	}

	void SubmitAuthorizedDraft(
		USeinWorldSubsystem& World,
		const FSeinCommand& Command)
	{
		if (SeinTestMatchBootstrap::Start(World))
		{
			World.SubmitLocalCommandDraft(Command);
		}
	}

	USeinAbility* GrantAbility(USeinWorldSubsystem& World,
		FSeinEntityHandle Entity, TSubclassOf<USeinAbility> AbilityClass,
		FGameplayTag AbilityTag)
	{
		const int32 AbilityID = USeinAbilityBPFL::SeinGrantAbility(
			&World, Entity, AbilityClass);
		USeinAbility* Ability = World.GetAbilityInstance(AbilityID);
		if (Ability)
		{
			Ability->AbilityTag = AbilityTag;
		}
		return Ability;
	}

	void SetSingleResourceCost(USeinAbility& Ability)
	{
		Ability.ResourceCost.Amounts.Reset();
		Ability.ResourceCost.Amounts.Add(
			SeinARTSTags::Resource, FFixedPoint::FromInt(AbilityCost));
	}

	int64 ResourceValue(const USeinWorldSubsystem& World, FSeinPlayerID Player)
	{
		const FSeinPlayerState* State = World.GetPlayerState(Player);
		return State
			? State->GetResource(SeinARTSTags::Resource).Value
			: MIN_int64;
	}

	int64 CostValue(const FSeinResourceCost& Cost)
	{
		return Cost.Amounts.FindRef(SeinARTSTags::Resource).Value;
	}

	FSeinProductionQueueEntry MakeReadyUnitEntry(
		FSeinPlayerID Payer,
		int32 CompletionCost = 0)
	{
		FSeinProductionQueueEntry Entry;
		Entry.ActorClass = ASeinProductionCostTestActor::StaticClass();
		Entry.TotalBuildTime = FFixedPoint::Zero;
		Entry.ResourcePayer = Payer;
		if (CompletionCost > 0)
		{
			Entry.AtCompletionCost.Amounts.Add(
				SeinARTSTags::Resource,
				FFixedPoint::FromInt(CompletionCost));
		}
		return Entry;
	}

	class FDestroyEntityPostTickSystem final : public ISeinSystem
	{
	public:
		virtual void Tick(FFixedPoint, USeinWorldSubsystem& InWorld) override
		{
			InWorld.GetEntityPool().ForEachEntity(
				[&InWorld](
					FSeinEntityHandle Handle,
					const FSeinEntity&)
			{
				FSeinDeferredDestroyTestComponent* Marker =
					InWorld.GetComponentMutable<
						FSeinDeferredDestroyTestComponent>(Handle);
				if (!Marker || !Marker->bArmed)
				{
					return;
				}
				Marker->bArmed = false;
				InWorld.DestroyEntity(Handle);
			});
		}
		virtual FSeinSystemDescriptor DescribeSystem() const override
		{
			return FSeinSystemDescriptor::Stateless(
				FName(TEXT("seinarts.tests.production.deferred_destroy")),
				1u,
				ESeinTickPhase::PostTick,
				0);
		}
	};

	struct FScopedResearchReplacementEffects
	{
		FScopedResearchReplacementEffects(FGameplayTag InRemovedTag,
			FGameplayTag InResearchTag)
			: Existing(*GetMutableDefault<USeinEffectPeriodicATestEffect>())
			, Replacement(*GetMutableDefault<USeinEffectPeriodicBTestEffect>())
			, PreviousExistingTag(Existing.EffectTag)
			, PreviousReplacementTag(Replacement.EffectTag)
			, PreviousRemovalTags(Replacement.RemoveEffectsWithTag)
		{
			Existing.EffectTag = InRemovedTag;
			Replacement.EffectTag = InResearchTag;
			Replacement.RemoveEffectsWithTag.Reset();
			Replacement.RemoveEffectsWithTag.AddTag(InRemovedTag);
		}

		~FScopedResearchReplacementEffects()
		{
			Existing.EffectTag = PreviousExistingTag;
			Replacement.EffectTag = PreviousReplacementTag;
			Replacement.RemoveEffectsWithTag = MoveTemp(PreviousRemovalTags);
			USeinEffectMutationTestHook::Callback = nullptr;
		}

		USeinEffect& Existing;
		USeinEffect& Replacement;
		FGameplayTag PreviousExistingTag;
		FGameplayTag PreviousReplacementTag;
		FGameplayTagContainer PreviousRemovalTags;
	};
}

namespace UE::SeinARTSTests
{
	TEST(AutoMoveThenUsesImmediatePolicyForPreviewPreflightAndCommit,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FScopedDefaultBrokerResolver BrokerResolver;
		// Catalog completion timing must not make an ordinary ability free.
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Entity;
		USeinAbility* MoveAbility = nullptr;
		USeinAbility* PaidAbility = nullptr;
		USeinAbility* AlternateMove = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));

			Entity = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Entity, FSeinAbilityPayload());
			AlternateMove = GrantAbility(*World, Entity,
				USeinDefaultMoveTestFirstAbility::StaticClass(),
				SeinARTSTags::Command_Context_RightClick);
			MoveAbility = GrantAbility(*World, Entity,
				USeinProductionCostTestMoveAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Ground);
			PaidAbility = GrantAbility(*World, Entity,
				USeinProductionCostTestAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (MoveAbility && PaidAbility)
			{
				MoveAbility->TargetType = ESeinAbilityTargetType::Point;
				FSeinMovementPayload Movement;
				Movement.DefaultMoveAbility = MoveAbility->GetClass();
				World->AddComponent(Entity, Movement);
				PaidAbility->MaxRange = FFixedPoint::FromInt(1);
				PaidAbility->OutOfRangeBehavior =
					ESeinOutOfRangeBehavior::AutoMoveThen;
				SetSingleResourceCost(*PaidAbility);
			}
			if (FSeinPlayerState* State =
				World->GetPlayerStateMutable(Player))
			{
				State->SetResource(
					SeinARTSTags::Resource, FFixedPoint::Zero);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(MoveAbility));
		ASSERT_THAT(IsNotNull(PaidAbility));

		const FFixedVector FarTarget(
			FFixedPoint::FromInt(100), FFixedPoint::Zero, FFixedPoint::Zero);
		const FSeinAbilityAvailability UnfundedAvailability =
			USeinAbilityBPFL::SeinGetAbilityAvailability(
				World, Entity, PaidAbility->AbilityTag,
				FSeinEntityHandle::Invalid(), FarTarget);
		ASSERT_THAT(IsFalse(UnfundedAvailability.bCanAfford));
		ASSERT_THAT(IsTrue(UnfundedAvailability.Reason
			== ESeinAbilityUnavailableReason::Unaffordable));

		FSeinCommand Command = FSeinCommand::MakeAbilityCommand(
			Player, Entity, PaidAbility->AbilityTag,
			FSeinEntityHandle::Invalid(), FarTarget);
		SubmitAuthorizedDraft(*World, Command);
		TickOnce(*World);
		TickOnce(*World); // A wrongly-deferred preflight would dispatch Move here.
		ASSERT_THAT(IsFalse(MoveAbility->bIsActive));
		ASSERT_THAT(IsFalse(PaidAbility->bIsActive));
		ASSERT_THAT(AreEqual(
			FFixedPoint::Zero.Value, ResourceValue(*World, Player)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinPlayerState* State =
				World->GetPlayerStateMutable(Player);
			ASSERT_THAT(IsNotNull(State));
			State->SetResource(
				SeinARTSTags::Resource, FFixedPoint::FromInt(StartingBalance));
		}
		const FSeinAbilityAvailability FundedAvailability =
			USeinAbilityBPFL::SeinGetAbilityAvailability(
				World, Entity, PaidAbility->AbilityTag,
				FSeinEntityHandle::Invalid(), FarTarget);
		ASSERT_THAT(IsTrue(FundedAvailability.bAvailable));
		SubmitAuthorizedDraft(*World, Command);
		TickOnce(*World);

		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(IsFalse(PaidAbility->bIsActive));
		ASSERT_THAT(IsTrue(PaidAbility->DeductedCost.IsEmpty()));
		ASSERT_THAT(IsTrue(PaidAbility->PendingCompletionCost.IsEmpty()));

		// The prefix already captured its movement tag. Changing the default
		// affects future resolution without rewriting this queued order.
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->GetComponentMutable<FSeinMovementPayload>(Entity)->DefaultMoveAbility = AlternateMove->GetClass();
		}
		ASSERT_THAT(IsTrue(World->ResolveDefaultMoveAbility(Entity) == AlternateMove));
		ASSERT_THAT(IsTrue(USeinAbilityBPFL::SeinGetAbilityAvailability(
			World, Entity, PaidAbility->AbilityTag,
			FSeinEntityHandle::Invalid(), FarTarget).bAvailable));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->GetComponentMutable<FSeinMovementPayload>(Entity)->DefaultMoveAbility = nullptr;
		}
		ASSERT_THAT(IsFalse(USeinAbilityBPFL::SeinGetAbilityAvailability(
			World, Entity, PaidAbility->AbilityTag,
			FSeinEntityHandle::Invalid(), FarTarget).bAvailable));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinEntity* SimEntity =
				World->GetEntityMutable(Entity);
			ASSERT_THAT(IsNotNull(SimEntity));
			SimEntity->Transform.SetLocation(FarTarget);
		}
		TickOnce(*World); // Consume the derived Move command.
		ASSERT_THAT(IsTrue(MoveAbility->bIsActive));
		ASSERT_THAT(IsFalse(AlternateMove->bIsActive));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			MoveAbility->EndAbility();
		}
		TickOnce(*World); // Broker observes Move completion and dispatches follow-up.
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));

		TickOnce(*World); // Consume the follow-up through the ordinary activation gate.
		ASSERT_THAT(IsTrue(PaidAbility->bIsActive));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));
		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));
	}

	TEST(ProductionRallyUsesSelectedGrantRatherThanFirstGrant,
		"SeinARTS.Sim.Abilities.DefaultMove")
	{
		FScopedDefaultBrokerResolver BrokerResolver;
		FScopedProducibleClass Producible(FFixedPoint::Zero);
		const auto* First = GetDefault<USeinDefaultMoveTestFirstAbility>();
		const auto* Selected = GetDefault<USeinDefaultMoveTestSelectedAbility>();
		FSeinAbilityPayload Grants;
		Grants.GrantedAbilities = {First->GetClass(), Selected->GetClass()};
		FSeinMovementPayload Movement;
		Movement.DefaultMoveAbility = Selected->GetClass();
		Producible.Bridge->ComponentData.Add(FInstancedStruct::Make(Grants));
		Producible.Bridge->ComponentData.Add(FInstancedStruct::Make(Movement));
		FActorTestSpawner Spawner;
		auto* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			const auto Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload Production;
			Production.Queue.Add(MakeReadyUnitEntry(Player));
			Production.RallyTransform.SetLocation(FFixedVector(
				FFixedPoint::FromInt(100), FFixedPoint::Zero, FFixedPoint::Zero));
			World->AddComponent(Producer, Production);
		})));
		TickOnce(*World);
		TickOnce(*World);
		FSeinEntityHandle Produced;
		World->GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity&)
		{
			if (World->GetEntityActorClass(Handle) == ASeinProductionCostTestActor::StaticClass()) Produced = Handle;
		});
		ASSERT_THAT(IsTrue(Produced.IsValid()));
		const auto* Abilities = World->GetComponent<FSeinAbilityPayload>(Produced);
		ASSERT_THAT(IsNotNull(Abilities));
		const auto* SelectedInstance = Abilities->FindAbilityByTag(*World, Selected->AbilityTag);
		const auto* FirstInstance = Abilities->FindAbilityByTag(*World, First->AbilityTag);
		ASSERT_THAT(IsNotNull(SelectedInstance));
		ASSERT_THAT(IsNotNull(FirstInstance));
		ASSERT_THAT(IsTrue(SelectedInstance->bIsActive));
		ASSERT_THAT(IsFalse(FirstInstance->bIsActive));
	}

	TEST(SuccessfulEnqueueTransfersFundingOwnershipAndCancellationRefundsOnce,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		TestRunner->AddExpectedError(
			TEXT("EnqueueProduction rejected outside this world's simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtEnqueue);
		FScopedProducibleClass Producible(FFixedPoint::FromInt(10));
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		const FSeinPlayerID NewOwner(2);
		FSeinEntityHandle Producer;
		USeinAbility* Ability = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			World->RegisterPlayer(NewOwner, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Producer, FSeinProductionPayload());
			World->AddComponent(Producer, FSeinAbilityPayload());
			Ability = GrantAbility(*World, Producer,
				USeinProductionCostTestAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (Ability)
			{
				Ability->CostTiming = ESeinAbilityCostTiming::ProductionQueue;
				SetSingleResourceCost(*Ability);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Ability));

		FSeinCommand Activate = FSeinCommand::MakeAbilityCommand(
			Player, Producer, Ability->AbilityTag);
		SubmitAuthorizedDraft(*World, Activate);
		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(AbilityCost).Value,
			CostValue(Ability->DeductedCost)));
		ASSERT_THAT(IsTrue(Ability->ResourcePayer == Player));

		Ability->EnqueueProduction(ASeinProductionCostTestActor::StaticClass());
		const FSeinProductionPayload* Production =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(0, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(AbilityCost).Value,
			CostValue(Ability->DeductedCost)));
		ASSERT_THAT(IsTrue(Ability->ResourcePayer == Player));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Ability->EnqueueProduction(ASeinProductionCostTestActor::StaticClass());
			// The first enqueue consumes the activation funding snapshot. A second
			// graph call must not mint another queue entry for free.
			Ability->EnqueueProduction(ASeinProductionCostTestActor::StaticClass());
			Ability->CancelAbility();
		}

		Production = World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(1, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(AbilityCost).Value,
			CostValue(Production->Queue[0].AtEnqueueCost)));
		ASSERT_THAT(IsTrue(Production->Queue[0].AtCompletionCost.IsEmpty()));
		ASSERT_THAT(IsTrue(Production->Queue[0].ResourcePayer == Player));
		ASSERT_THAT(IsTrue(Ability->DeductedCost.IsEmpty()));
		ASSERT_THAT(IsTrue(Ability->PendingCompletionCost.IsEmpty()));
		ASSERT_THAT(IsFalse(Ability->ResourcePayer.IsValid()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			World->SetEntityOwner(Producer, NewOwner);
		}
		// Ownership transfer now cancels pending production with normal refunds.
		TickOnce(*World);
		Production = World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(0, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, NewOwner)));
	}

	TEST(AbilityCancelsSelectedProductionWithCapturedRefundPolicy,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		TestRunner->AddExpectedError(
			TEXT("CancelProduction rejected outside this world's simulation context"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtEnqueue);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		const FSeinPlayerID Payer(1);
		const FSeinPlayerID Owner(2);
		FSeinEntityHandle Producer;
		USeinAbility* Ability = NewObject<USeinProductionCostTestAbility>(World);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]()
		{
			World->RegisterPlayer(Payer, FSeinFactionID(1));
			World->RegisterPlayer(Owner, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Owner);
			FSeinProductionQueueEntry Entry = MakeReadyUnitEntry(Payer, 70);
			Entry.TotalBuildTime = FFixedPoint::FromInt(10);
			Entry.AtEnqueueCost.Amounts.Add(
				SeinARTSTags::Resource, FFixedPoint::FromInt(20));
			FSeinProductionPayload Production;
			Production.Queue = {Entry, Entry, Entry};
			Production.Queue[2].RefundPolicy.bUseCustomRefund = true;
			Production.Queue[2].RefundPolicy.CustomRefundPercentage =
				FFixedPoint::One / FFixedPoint::FromInt(4);
			Production.CurrentBuildProgress = FFixedPoint::FromInt(5);
			Production.bStalledAtCompletion = true;
			World->AddComponent(Producer, Production);
			Ability->InitializeAbility(Producer, World);
		})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		ASSERT_THAT(IsFalse(Ability->CancelProduction()));
		const auto* Production = World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(3, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Payer)));

		auto SimScope = FSeinSimContextTestAccess::Enter(*World);
		ASSERT_THAT(IsFalse(Ability->CancelProduction(-1)));
		ASSERT_THAT(IsFalse(Ability->CancelProduction(3)));
		ASSERT_THAT(AreEqual(3, Production->Queue.Num()));
		ASSERT_THAT(IsTrue(Ability->CancelProduction(2)));
		ASSERT_THAT(AreEqual(2, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(StartingBalance + 5).Value,
			ResourceValue(*World, Payer)));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(5).Value,
			Production->CurrentBuildProgress.Value));
		ASSERT_THAT(IsTrue(Production->bStalledAtCompletion));

		ASSERT_THAT(IsTrue(Ability->CancelProduction(1)));
		ASSERT_THAT(AreEqual(1, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(StartingBalance + 25).Value,
			ResourceValue(*World, Payer)));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(5).Value,
			Production->CurrentBuildProgress.Value));
		ASSERT_THAT(IsTrue(Production->bStalledAtCompletion));

		ASSERT_THAT(IsTrue(Ability->CancelProduction()));
		ASSERT_THAT(AreEqual(0, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(FFixedPoint::Zero.Value,
			Production->CurrentBuildProgress.Value));
		ASSERT_THAT(IsFalse(Production->bStalledAtCompletion));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(StartingBalance + 35).Value,
			ResourceValue(*World, Payer)));
		ASSERT_THAT(IsFalse(Ability->CancelProduction()));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(StartingBalance + 35).Value,
			ResourceValue(*World, Payer)));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Owner)));

		World->RemoveComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsFalse(Ability->CancelProduction()));
		Ability->InitializeAbility(Producer, nullptr);
		ASSERT_THAT(IsFalse(Ability->CancelProduction()));
	}

	TEST(ImmediateProductionIgnoresCatalogDeferralAndQueuesNoCompletionCost,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FScopedProducibleClass Producible(FFixedPoint::FromInt(10));
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		USeinAbility* Ability = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Producer, FSeinProductionPayload());
			World->AddComponent(Producer, FSeinAbilityPayload());
			Ability = GrantAbility(*World, Producer,
				USeinProductionCostTestAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (Ability)
			{
				SetSingleResourceCost(*Ability);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Ability));
		ASSERT_THAT(IsTrue(
			Ability->CostTiming == ESeinAbilityCostTiming::Immediate));

		SubmitAuthorizedDraft(*World, FSeinCommand::MakeAbilityCommand(
			Player, Producer, Ability->AbilityTag));
		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(AbilityCost).Value,
			CostValue(Ability->DeductedCost)));
		ASSERT_THAT(IsTrue(Ability->PendingCompletionCost.IsEmpty()));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Ability->EnqueueProduction(
				ASeinProductionCostTestActor::StaticClass());
		}
		const FSeinProductionPayload* Production =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(1, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(AbilityCost).Value,
			CostValue(Production->Queue[0].AtEnqueueCost)));
		ASSERT_THAT(IsTrue(
			Production->Queue[0].AtCompletionCost.IsEmpty()));
	}

	TEST(FailedEnqueueRollsBackActivationFundingWithoutDoubleRefund,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtEnqueue);
		FScopedProducibleClass Producible(FFixedPoint::FromInt(10));
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		USeinAbility* Ability = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload InitialProduction;
			InitialProduction.MaxQueueSize = 1;
			FSeinProductionQueueEntry ExistingEntry;
			ExistingEntry.TotalBuildTime = FFixedPoint::FromInt(1000);
			InitialProduction.Queue.Add(ExistingEntry);
			World->AddComponent(Producer, InitialProduction);
			World->AddComponent(Producer, FSeinAbilityPayload());
			Ability = GrantAbility(*World, Producer,
				USeinProductionCostTestAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (Ability)
			{
				Ability->CostTiming = ESeinAbilityCostTiming::ProductionQueue;
				SetSingleResourceCost(*Ability);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Ability));

		FSeinCommand Activate = FSeinCommand::MakeAbilityCommand(
			Player, Producer, Ability->AbilityTag);
		SubmitAuthorizedDraft(*World, Activate);
		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Ability->EnqueueProduction(ASeinProductionCostTestActor::StaticClass());
			Ability->CancelAbility();
		}

		const FSeinProductionPayload* Production =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(1, Production->Queue.Num()));
		ASSERT_THAT(IsTrue(Ability->DeductedCost.IsEmpty()));
		ASSERT_THAT(IsTrue(Ability->PendingCompletionCost.IsEmpty()));
		ASSERT_THAT(IsFalse(Ability->ResourcePayer.IsValid()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
	}

	TEST(CompletionCostStallsThenChargesItsSnapshottedPayerOnce,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FScopedProducibleClass Producible(FFixedPoint::Zero);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		const FSeinPlayerID NewOwner(2);
		FSeinEntityHandle Producer;
		USeinAbility* Ability = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			World->RegisterPlayer(NewOwner, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Producer, FSeinProductionPayload());
			World->AddComponent(Producer, FSeinAbilityPayload());
			Ability = GrantAbility(*World, Producer,
				USeinProductionCostTestAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (Ability)
			{
				Ability->CostTiming = ESeinAbilityCostTiming::ProductionQueue;
				SetSingleResourceCost(*Ability);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Ability));

		FSeinCommand Activate = FSeinCommand::MakeAbilityCommand(
			Player, Producer, Ability->AbilityTag);
		SubmitAuthorizedDraft(*World, Activate);
		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(IsTrue(Ability->DeductedCost.IsEmpty()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(AbilityCost).Value,
			CostValue(Ability->PendingCompletionCost)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			// Transfer before enqueue: the activation's captured payer remains Player,
			// while production belongs to NewOwner. Pending queues would be cancelled.
			World->SetEntityOwner(Producer, NewOwner);
			Ability->EnqueueProduction(ASeinProductionCostTestActor::StaticClass());
			Ability->CancelAbility();
			FSeinPlayerState* State =
				World->GetPlayerStateMutable(Player);
			ASSERT_THAT(IsNotNull(State));
			State->SetResource(SeinARTSTags::Resource, FFixedPoint::Zero);
		}

		const int32 EntityCountBeforeCompletion =
			World->GetEntityPool().GetActiveCount();
		TickOnce(*World);
		const FSeinProductionPayload* Production =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(1, Production->Queue.Num()));
		ASSERT_THAT(IsTrue(Production->bStalledAtCompletion));
		ASSERT_THAT(AreEqual(EntityCountBeforeCompletion,
			World->GetEntityPool().GetActiveCount()));
		ASSERT_THAT(AreEqual(FFixedPoint::Zero.Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, NewOwner)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinPlayerState* State =
				World->GetPlayerStateMutable(Player);
			ASSERT_THAT(IsNotNull(State));
			State->SetResource(
				SeinARTSTags::Resource, FFixedPoint::FromInt(AbilityCost));
		}
		TickOnce(*World);
		Production = World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(0, Production->Queue.Num()));
		ASSERT_THAT(IsFalse(Production->bStalledAtCompletion));
		ASSERT_THAT(AreEqual(EntityCountBeforeCompletion + 1,
			World->GetEntityPool().GetActiveCount()));
		ASSERT_THAT(AreEqual(FFixedPoint::Zero.Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, NewOwner)));

		FSeinPlayerID ProducedOwner = FSeinPlayerID::Neutral();
		World->GetEntityPool().ForEachEntity(
			[&](FSeinEntityHandle Handle, const FSeinEntity&)
			{
				if (Handle != Producer)
				{
					ProducedOwner = World->GetEntityOwner(Handle);
				}
			});
		ASSERT_THAT(IsTrue(ProducedOwner == NewOwner));

		TickOnce(*World);
		ASSERT_THAT(AreEqual(FFixedPoint::Zero.Value,
			ResourceValue(*World, Player)));
	}

	TEST(CompletionSurvivesEntityPoolAndProductionStorageGrowth,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FScopedProducibleClass Producible(
			FFixedPoint::Zero, false, true);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		int32 CountBefore = 0;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload InitialProduction;
			InitialProduction.Queue.Add(MakeReadyUnitEntry(Player));
			World->AddComponent(Producer, InitialProduction);

			const int32 InitialCapacity = World->GetEntityPool().GetCapacity();
			while (World->GetEntityPool().GetActiveCount() < InitialCapacity)
			{
				ASSERT_THAT(IsTrue(World->SpawnAbstractEntity(
					FFixedTransform(), Player).IsValid()));
			}
			CountBefore = World->GetEntityPool().GetActiveCount();
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		TickOnce(*World);
		const FSeinProductionPayload* Production =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(0, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(
			CountBefore + 1, World->GetEntityPool().GetActiveCount()));

		int32 ProducedCount = 0;
		World->GetEntityPool().ForEachEntity(
			[&](FSeinEntityHandle Handle, const FSeinEntity&)
			{
				ProducedCount += World->GetEntityActorClass(Handle)
					== ASeinProductionCostTestActor::StaticClass() ? 1 : 0;
			});
		ASSERT_THAT(AreEqual(1, ProducedCount));

		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			CountBefore + 1, World->GetEntityPool().GetActiveCount()));
	}

	TEST(ReadyProducersSharingAPayerRecheckAffordabilitySequentially,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FScopedProducibleClass Producible(FFixedPoint::Zero);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle First;
		FSeinEntityHandle Second;
		int32 CountBefore = 0;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			First = World->SpawnAbstractEntity(FFixedTransform(), Player);
			Second = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload Production;
			Production.Queue.Add(MakeReadyUnitEntry(Player, 75));
			World->AddComponent(First, Production);
			World->AddComponent(Second, Production);
			CountBefore = World->GetEntityPool().GetActiveCount();
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		TickOnce(*World);
		const FSeinProductionPayload* FirstProduction =
			World->GetComponent<FSeinProductionPayload>(First);
		const FSeinProductionPayload* SecondProduction =
			World->GetComponent<FSeinProductionPayload>(Second);
		ASSERT_THAT(IsNotNull(FirstProduction));
		ASSERT_THAT(IsNotNull(SecondProduction));
		ASSERT_THAT(AreEqual(0, FirstProduction->Queue.Num()));
		ASSERT_THAT(AreEqual(1, SecondProduction->Queue.Num()));
		ASSERT_THAT(IsTrue(SecondProduction->bStalledAtCompletion));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(25).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			CountBefore + 1, World->GetEntityPool().GetActiveCount()));
	}

	TEST(InvalidUnitSpawnEntryStallsWithoutChargeOrDequeue,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		TestRunner->AddExpectedError(
			TEXT("unit entry has no actor class"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		int32 CountBefore = 0;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload Production;
			FSeinProductionQueueEntry InvalidEntry =
				MakeReadyUnitEntry(Player, AbilityCost);
			InvalidEntry.ActorClass = nullptr;
			Production.Queue.Add(InvalidEntry);
			World->AddComponent(Producer, Production);
			CountBefore = World->GetEntityPool().GetActiveCount();
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		TickOnce(*World);
		const FSeinProductionPayload* Current =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Current));
		ASSERT_THAT(AreEqual(1, Current->Queue.Num()));
		ASSERT_THAT(IsTrue(Current->bStalledAtCompletion));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			CountBefore, World->GetEntityPool().GetActiveCount()));

		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
	}

	TEST(InvalidResearchIsRejectedAtEnqueueAndCannotConsumeFunding,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		ExpectAbilityHashDiagnostic(*TestRunner);
		TestRunner->AddExpectedError(
			TEXT("has no usable GrantedTechEffect"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtEnqueue);
		FScopedProducibleClass Producible(
			FFixedPoint::Zero, true);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		USeinAbility* Ability = nullptr;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Producer, FSeinProductionPayload());
			World->AddComponent(Producer, FSeinAbilityPayload());
			Ability = GrantAbility(*World, Producer,
				USeinProductionCostTestAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (Ability)
			{
				SetSingleResourceCost(*Ability);
			}
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));
		ASSERT_THAT(IsNotNull(Ability));

		SubmitAuthorizedDraft(*World, FSeinCommand::MakeAbilityCommand(
			Player, Producer, Ability->AbilityTag));
		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			Ability->EnqueueProduction(
				ASeinProductionCostTestActor::StaticClass());
		}
		const FSeinProductionPayload* Current =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Current));
		ASSERT_THAT(AreEqual(0, Current->Queue.Num()));
		ASSERT_THAT(IsTrue(Ability->DeductedCost.IsEmpty()));
		ASSERT_THAT(IsFalse(Ability->ResourcePayer.IsValid()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
	}

	TEST(MalformedResearchQueueStallsWithoutChargeOrCompletion,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		TestRunner->AddExpectedError(
			TEXT("research entry has no usable effect class"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload Production;
			FSeinProductionQueueEntry Entry =
				MakeReadyUnitEntry(Player, AbilityCost);
			Entry.bIsResearch = true;
			Entry.ResearchEffectClass = nullptr;
			Production.Queue.Add(Entry);
			World->AddComponent(Producer, Production);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		TickOnce(*World);
		const FSeinProductionPayload* Current =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Current));
		ASSERT_THAT(AreEqual(1, Current->Queue.Num()));
		ASSERT_THAT(IsTrue(Current->bStalledAtCompletion));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(1, World->GetEntityPool().GetActiveCount()));
	}

	TEST(CleanResearchApplyRejectionRefundsRequeuesAndStalls,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		TestRunner->AddExpectedError(
			TEXT("has no storage for scope"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		USeinEffectPeriodicBTestEffect* Effect =
			GetMutableDefault<USeinEffectPeriodicBTestEffect>();
		ASSERT_THAT(IsNotNull(Effect));
		TGuardValue<ESeinModifierScope> ScopeGuard(
			Effect->Scope, ESeinModifierScope::Instance);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Payer(1);
		FSeinEntityHandle Producer;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Payer, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(
				FFixedTransform(), FSeinPlayerID::Neutral());
			FSeinProductionPayload Production;
			FSeinProductionQueueEntry Entry =
				MakeReadyUnitEntry(Payer, AbilityCost);
			Entry.bIsResearch = true;
			Entry.ResearchEffectClass =
				USeinEffectPeriodicBTestEffect::StaticClass();
			Production.Queue.Add(Entry);
			World->AddComponent(Producer, Production);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		TickOnce(*World);
		const FSeinProductionPayload* Current =
			World->GetComponent<FSeinProductionPayload>(Producer);
		ASSERT_THAT(IsNotNull(Current));
		ASSERT_THAT(AreEqual(1, Current->Queue.Num()));
		ASSERT_THAT(IsTrue(Current->bStalledAtCompletion));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Payer)));
	}

	TEST(ResearchReplacementInvalidationConsumesCostWithoutSuccessOrRetry,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		TestRunner->AddExpectedError(
			TEXT("replacement callbacks invalidated target"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		TestRunner->AddExpectedError(
			TEXT("cost and queue entry remain consumed"),
			EAutomationExpectedErrorFlags::Contains, 1, false);
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		FScopedResearchReplacementEffects Effects(
			SeinARTSTags::Environment_Default.GetTag(), SeinARTSTags::Resource);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload Production;
			FSeinProductionQueueEntry Entry =
				MakeReadyUnitEntry(Player, AbilityCost);
			Entry.bIsResearch = true;
			Entry.ResearchEffectClass =
				USeinEffectPeriodicBTestEffect::StaticClass();
			Production.Queue.Add(Entry);
			World->AddComponent(Producer, Production);
			World->AddComponent(Producer, FSeinActiveEffectsPayload());
			ASSERT_THAT(IsTrue(World->ApplyEffect(
				Producer,
				USeinEffectPeriodicATestEffect::StaticClass(),
				Producer) > 0));
			World->FlushVisualEvents();
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		USeinEffectMutationTestHook::Callback =
			[&](USeinEffectMutationTestHook& Effect, FName EventName,
				FSeinEntityHandle Target)
			{
				if (Effect.IsA<USeinEffectPeriodicATestEffect>()
					&& EventName == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnRemoved))
				{
					World->DestroyEntity(Target);
				}
			};

		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));
		const TArray<FSeinVisualEvent> Events = World->FlushVisualEvents();
		ASSERT_THAT(IsFalse(Events.ContainsByPredicate(
			[](const FSeinVisualEvent& Event)
			{
				return Event.Type == ESeinVisualEventType::ProductionCompleted
					|| Event.Type == ESeinVisualEventType::TechResearched;
			})));

		TickOnce(*World);
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));
	}

	TEST(ResearchEffectCommitsBeforeProducerDestructionLaterThatTick,
		"SeinARTS.Sim.Abilities.ProductionCost")
	{
		FScopedResourceCatalog Catalog(ESeinProductionDeductionTiming::AtCompletion);
		// Declared before the spawner so world teardown drops the registration
		// before the stack-owned system is destroyed.
		FDestroyEntityPostTickSystem DestroyProducer;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASSERT_THAT(IsTrue(World->RegisterSystem(&DestroyProducer)));

		USeinProductionCostTestResearchEffect* Effect =
			GetMutableDefault<USeinProductionCostTestResearchEffect>();
		ASSERT_THAT(IsNotNull(Effect));
		TGuardValue<ESeinModifierScope> ScopeGuard(
			Effect->Scope, ESeinModifierScope::Player);
		TGuardValue<ESeinEffectDurationMode> DurationGuard(
			Effect->DurationMode, ESeinEffectDurationMode::Persistent);
		TGuardValue<bool> SourceDeathGuard(
			Effect->bRemoveOnSourceDeath, false);

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Producer;
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload Production;
			FSeinProductionQueueEntry Entry =
				MakeReadyUnitEntry(Player, AbilityCost);
			Entry.bIsResearch = true;
			Entry.ResearchEffectClass =
				USeinProductionCostTestResearchEffect::StaticClass();
			Production.Queue.Add(Entry);
			World->AddComponent(Producer, Production);
			FSeinDeferredDestroyTestComponent DestroyMarker;
			DestroyMarker.bArmed = true;
			World->AddComponent(Producer, DestroyMarker);
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		TickOnce(*World);
		const FSeinPlayerState* State = World->GetPlayerState(Player);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(1, State->PlayerEffects.Num()));
		ASSERT_THAT(IsTrue(State->PlayerEffects[0].EffectClass
			== USeinProductionCostTestResearchEffect::StaticClass()));
		ASSERT_THAT(IsFalse(World->IsEntityAlive(Producer)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance - AbilityCost).Value,
			ResourceValue(*World, Player)));

		TickOnce(*World);
		State = World->GetPlayerState(Player);
		ASSERT_THAT(IsNotNull(State));
		ASSERT_THAT(AreEqual(1, State->PlayerEffects.Num()));
		ASSERT_THAT(IsFalse(World->GetEntityPool().IsValid(Producer)));
	}
}
