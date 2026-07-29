#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinEntityComponent.h"
#include "Brokers/SeinDefaultCommandBrokerResolver.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinActiveEffectsComponent.h"
#include "Components/SeinProducibleComponent.h"
#include "Components/SeinProductionComponent.h"
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
			TArray<const USeinEntityComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityComponent>(
				ASeinProductionCostTestActor::StaticClass(), Bridges);
			check(!Bridges.IsEmpty());
			Bridge = const_cast<USeinEntityComponent*>(Bridges[0]);
			PreviousComponentData = Bridge->ComponentData;

			Bridge->ComponentData.Reset();
			FSeinProducibleComponent Producible;
			Producible.BuildTime = BuildTime;
			Producible.bIsResearch = bIsResearch;
			Bridge->ComponentData.Add(FInstancedStruct::Make(Producible));
			if (bIncludeProductionComponent)
			{
				Bridge->ComponentData.Add(
					FInstancedStruct::Make(FSeinProductionComponent()));
			}
		}

		~FScopedProducibleClass()
		{
			Bridge->ComponentData = MoveTemp(PreviousComponentData);
		}

		USeinEntityComponent* Bridge = nullptr;
		TArray<FInstancedStruct> PreviousComponentData;
	};

	template <typename TTestRunner>
	void ExpectAbilityHashDiagnostic(TTestRunner& TestRunner)
	{
		TestRunner.AddExpectedError(
			TEXT("Component 'SeinAbilityComponent' has field(s) excluded from the legacy local state fingerprint"),
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
					FSeinEntity&)
			{
				FSeinDeferredDestroyTestComponent* Marker =
					InWorld.GetComponent<
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
		const auto AuthorState = [&]()
		{
			World->RegisterPlayer(Player, FSeinFactionID(1));

			Entity = World->SpawnAbstractEntity(FFixedTransform(), Player);
			World->AddComponent(Entity, FSeinAbilityComponent());
			MoveAbility = GrantAbility(*World, Entity,
				USeinProductionCostTestMoveAbility::StaticClass(),
				SeinARTSTags::Command_Context_Target_Ground);
			PaidAbility = GrantAbility(*World, Entity,
				USeinProductionCostTestAbility::StaticClass(),
				SeinARTSTags::Command_Context_AbilityTriggered);
			if (MoveAbility && PaidAbility)
			{
				MoveAbility->bIsMoveAbility = true;
				PaidAbility->MaxRange = FFixedPoint::FromInt(1);
				PaidAbility->OutOfRangeBehavior =
					ESeinOutOfRangeBehavior::AutoMoveThen;
				SetSingleResourceCost(*PaidAbility);
			}
			if (FSeinPlayerState* State = World->GetPlayerState(Player))
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
			FSeinPlayerState* State = World->GetPlayerState(Player);
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

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinEntity* SimEntity = World->GetEntity(Entity);
			ASSERT_THAT(IsNotNull(SimEntity));
			SimEntity->Transform.SetLocation(FarTarget);
		}
		TickOnce(*World); // Consume the derived Move command.
		ASSERT_THAT(IsTrue(MoveAbility->bIsActive));

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
			World->AddComponent(Producer, FSeinProductionComponent());
			World->AddComponent(Producer, FSeinAbilityComponent());
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
		const FSeinProductionComponent* Production =
			World->GetComponent<FSeinProductionComponent>(Producer);
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

		Production = World->GetComponent<FSeinProductionComponent>(Producer);
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
		FSeinCommand Cancel = FSeinCommand::MakeCancelProductionCommand(
			NewOwner, Producer, 0);
		SubmitAuthorizedDraft(*World, Cancel);
		TickOnce(*World);
		Production = World->GetComponent<FSeinProductionComponent>(Producer);
		ASSERT_THAT(IsNotNull(Production));
		ASSERT_THAT(AreEqual(0, Production->Queue.Num()));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, Player)));
		ASSERT_THAT(AreEqual(
			FFixedPoint::FromInt(StartingBalance).Value,
			ResourceValue(*World, NewOwner)));
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
			World->AddComponent(Producer, FSeinProductionComponent());
			World->AddComponent(Producer, FSeinAbilityComponent());
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
		const FSeinProductionComponent* Production =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			FSeinProductionComponent InitialProduction;
			InitialProduction.MaxQueueSize = 1;
			FSeinProductionQueueEntry ExistingEntry;
			ExistingEntry.TotalBuildTime = FFixedPoint::FromInt(1000);
			InitialProduction.Queue.Add(ExistingEntry);
			World->AddComponent(Producer, InitialProduction);
			World->AddComponent(Producer, FSeinAbilityComponent());
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

		const FSeinProductionComponent* Production =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			World->AddComponent(Producer, FSeinProductionComponent());
			World->AddComponent(Producer, FSeinAbilityComponent());
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
			Ability->EnqueueProduction(ASeinProductionCostTestActor::StaticClass());
			Ability->CancelAbility();
			World->SetEntityOwner(Producer, NewOwner);
			FSeinPlayerState* State = World->GetPlayerState(Player);
			ASSERT_THAT(IsNotNull(State));
			State->SetResource(SeinARTSTags::Resource, FFixedPoint::Zero);
		}

		const int32 EntityCountBeforeCompletion =
			World->GetEntityPool().GetActiveCount();
		TickOnce(*World);
		const FSeinProductionComponent* Production =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			FSeinPlayerState* State = World->GetPlayerState(Player);
			ASSERT_THAT(IsNotNull(State));
			State->SetResource(
				SeinARTSTags::Resource, FFixedPoint::FromInt(AbilityCost));
		}
		TickOnce(*World);
		Production = World->GetComponent<FSeinProductionComponent>(Producer);
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
			FSeinProductionComponent InitialProduction;
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
		const FSeinProductionComponent* Production =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			FSeinProductionComponent Production;
			Production.Queue.Add(MakeReadyUnitEntry(Player, 75));
			World->AddComponent(First, Production);
			World->AddComponent(Second, Production);
			CountBefore = World->GetEntityPool().GetActiveCount();
		};
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World, AuthorState)));

		TickOnce(*World);
		const FSeinProductionComponent* FirstProduction =
			World->GetComponent<FSeinProductionComponent>(First);
		const FSeinProductionComponent* SecondProduction =
			World->GetComponent<FSeinProductionComponent>(Second);
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
			FSeinProductionComponent Production;
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
		const FSeinProductionComponent* Current =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			World->AddComponent(Producer, FSeinProductionComponent());
			World->AddComponent(Producer, FSeinAbilityComponent());
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
		const FSeinProductionComponent* Current =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			FSeinProductionComponent Production;
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
		const FSeinProductionComponent* Current =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			FSeinProductionComponent Production;
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
		const FSeinProductionComponent* Current =
			World->GetComponent<FSeinProductionComponent>(Producer);
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
			FSeinProductionComponent Production;
			FSeinProductionQueueEntry Entry =
				MakeReadyUnitEntry(Player, AbilityCost);
			Entry.bIsResearch = true;
			Entry.ResearchEffectClass =
				USeinEffectPeriodicBTestEffect::StaticClass();
			Production.Queue.Add(Entry);
			World->AddComponent(Producer, Production);
			World->AddComponent(Producer, FSeinActiveEffectsComponent());
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
			FSeinProductionComponent Production;
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
