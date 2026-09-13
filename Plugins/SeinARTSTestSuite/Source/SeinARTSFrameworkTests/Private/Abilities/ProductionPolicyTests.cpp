#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinProduciblePayload.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "HAL/IConsoleManager.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Lib/SeinProductionBPFL.h"
#include "Lib/SeinResourceBPFL.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinProductionCostTestTypes.h"
#include "TestTypes/SeinProductionPolicyTestTypes.h"
#include "TestTypes/SeinEffectMutationTestTypes.h"

USeinProductionPolicyTestAbility::USeinProductionPolicyTestAbility()
{
	AbilityTag = SeinARTSTags::Command_Context_AbilityTriggered;
	ResourceCost.Amounts.Add(SeinARTSTags::Resource, FFixedPoint::FromInt(10));
}

void USeinProductionPolicyTestAbility::OnActivate_Implementation()
{
	EnqueueProduction(ASeinProductionCostTestActor::StaticClass());
	EndAbility();
}

namespace UE::SeinARTSTests
{
namespace ProductionPolicyTestLocal
{
	const FSeinPlayerID Player(1);
	const FSeinPlayerID OtherPlayer(2);
	UClass* ItemClass() { return ASeinProductionCostTestActor::StaticClass(); }

	struct FDefinition
	{
		USeinEntityBridgeComponent* Bridge = nullptr;
		TArray<FInstancedStruct> Previous;
		TArray<FSeinResourceDefinition> PreviousCatalog;
		FDefinition(ESeinProductionQueuePolicy Policy = ESeinProductionQueuePolicy::MultiQueueable,
			int32 Amount = 3, int32 BuildSeconds = 100)
		{
			TArray<const USeinEntityBridgeComponent*> Bridges;
			AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(ItemClass(), Bridges);
			Bridge = const_cast<USeinEntityBridgeComponent*>(Bridges[0]);
			Previous = Bridge->ComponentData;
			FSeinProduciblePayload Item;
			Item.QueuePolicy = Policy;
			Item.QueueAmount = Amount;
			Item.BuildTime = FFixedPoint::FromInt(BuildSeconds);
			Bridge->ComponentData = {FInstancedStruct::Make(Item)};
			auto* Settings = GetMutableDefault<USeinARTSCoreSettings>();
			PreviousCatalog = Settings->ResourceCatalog;
			FSeinResourceDefinition Resource;
			Resource.ResourceTag = SeinARTSTags::Resource;
			Resource.DefaultStartingValue = FFixedPoint::FromInt(100);
			Settings->ResourceCatalog = {Resource};
		}
		~FDefinition()
		{
			Bridge->ComponentData = MoveTemp(Previous);
			GetMutableDefault<USeinARTSCoreSettings>()->ResourceCatalog = MoveTemp(PreviousCatalog);
			USeinEffectMutationTestHook::Callback = nullptr;
		}
	};

	struct FFixture
	{
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		FSeinEntityHandle A, B, C;
		bool Initialize(bool bGrantAbility = false)
		{
			return SeinTestMatchBootstrap::Materialize(*World, [&]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));
				World->RegisterPlayer(OtherPlayer, FSeinFactionID(1));
				A = World->SpawnAbstractEntity(FFixedTransform(), Player);
				B = World->SpawnAbstractEntity(FFixedTransform(), Player);
				C = World->SpawnAbstractEntity(FFixedTransform(), OtherPlayer);
				for (const auto Handle : {A, B, C})
				{
					FSeinProductionPayload Production;
					Production.MaxQueueSize = 10;
					World->AddComponent(Handle, Production);
					if (bGrantAbility)
					{
						World->AddComponent(Handle, FSeinAbilityPayload());
						USeinAbilityBPFL::SeinGrantAbility(World, Handle, USeinProductionPolicyTestAbility::StaticClass());
					}
				}
			}) && SeinTestMatchBootstrap::Start(*World);
		}
		~FFixture() { World->StopSimulation(); }
		void Queue(FSeinEntityHandle Producer, int32 Paid = 0)
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			auto* Ability = NewObject<USeinProductionCostTestAbility>(World);
			Ability->InitializeAbility(Producer, World);
			Ability->ResourcePayer = World->GetEntityOwner(Producer);
			if (Paid)
			{
				Ability->DeductedCost.Amounts.Add(SeinARTSTags::Resource, FFixedPoint::FromInt(Paid));
				USeinResourceBPFL::SeinDeduct(World, Ability->ResourcePayer, Ability->DeductedCost);
			}
			Ability->EnqueueProduction(ItemClass());
		}
		int32 Depth(FSeinEntityHandle Producer) const
		{
			const auto* Production = World->GetComponent<FSeinProductionPayload>(Producer);
			return Production ? Production->Queue.Num() : 0;
		}
		ESeinProductionQueueResult Check(FSeinEntityHandle Producer, int64* OutUsed = nullptr) const
		{
			FSeinProductionQueueSettings Settings; int64 Used;
			const auto Result = World->CheckProductionQueue(Producer, ItemClass(), Settings, Used);
			if (OutUsed) *OutUsed = Used;
			return Result;
		}
		void Tick() { FTSTicker::GetCoreTicker().Tick(World->GetFixedDeltaTimeSeconds()); }
	};

	FSeinProductionQueueSettings Settings(ESeinProductionQueuePolicy Policy, int32 Amount = 3)
	{
		FSeinProductionQueueSettings Result;
		Result.QueuePolicy = Policy; Result.QueueAmount = Amount;
		return Result;
	}
}

	TEST(AuthoredPoliciesEnforceProducerAndPlayerLimits, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		for (const auto Policy : {ESeinProductionQueuePolicy::MultiQueueable,
			ESeinProductionQueuePolicy::OncePerProductionUnit, ESeinProductionQueuePolicy::OncePerPlayer,
			ESeinProductionQueuePolicy::FixedAmountPerProductionUnit, ESeinProductionQueuePolicy::FixedAmountPerPlayer})
		{
			FDefinition Definition(Policy); FFixture F;
			ASSERT_THAT(IsTrue(F.Initialize()));
			for (int32 Index = 0; Index < 4; ++Index) F.Queue(F.A);
			F.Queue(F.B); F.Queue(F.C);
			const bool bOnce = Policy == ESeinProductionQueuePolicy::OncePerProductionUnit || Policy == ESeinProductionQueuePolicy::OncePerPlayer;
			const bool bPlayer = Policy == ESeinProductionQueuePolicy::OncePerPlayer || Policy == ESeinProductionQueuePolicy::FixedAmountPerPlayer;
			ASSERT_THAT(AreEqual(Policy == ESeinProductionQueuePolicy::MultiQueueable ? 4 : (bOnce ? 1 : 3), F.Depth(F.A)));
			ASSERT_THAT(AreEqual(bPlayer ? 0 : 1, F.Depth(F.B)));
			ASSERT_THAT(AreEqual(1, F.Depth(F.C)));
			if (Policy != ESeinProductionQueuePolicy::MultiQueueable)
			{
				auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
				ASSERT_THAT(IsTrue(F.World->CancelProduction(F.A, 0)));
				ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::Available));
			}
		}
	}

	TEST(CompletedHistorySurvivesPolicyChangesAndProductionComponentReplacement, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition(ESeinProductionQueuePolicy::MultiQueueable, 3, 0); FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize()));
		F.Queue(F.A); F.Tick();
		ASSERT_THAT(AreEqual(0, F.Depth(F.A)));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		auto Once = Settings(ESeinProductionQueuePolicy::OncePerPlayer);
		ASSERT_THAT(IsTrue(F.World->SetPlayerQueueSettings(Player, ItemClass(), &Once)));
		ASSERT_THAT(IsTrue(F.Check(F.B) == ESeinProductionQueueResult::LimitReached));
		ASSERT_THAT(IsTrue(F.Check(F.C) == ESeinProductionQueueResult::Available));
		auto UnitOnce = Settings(ESeinProductionQueuePolicy::OncePerProductionUnit);
		ASSERT_THAT(IsTrue(F.World->SetProducerQueueSettings(F.A, ItemClass(), &UnitOnce)));
		F.World->RemoveComponent<FSeinProductionPayload>(F.A);
		F.World->AddComponent(F.A, FSeinProductionPayload());
		ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::LimitReached));
		auto Raised = Settings(ESeinProductionQueuePolicy::FixedAmountPerPlayer, 3);
		ASSERT_THAT(IsTrue(F.World->SetPlayerQueueSettings(Player, ItemClass(), &Raised)));
		ASSERT_THAT(IsTrue(F.Check(F.B) == ESeinProductionQueueResult::Available));
		ASSERT_THAT(IsTrue(F.World->SetProducerQueueSettings(F.A, ItemClass(), nullptr)));
		ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::Available));
	}

	TEST(RuntimeLoweringPreservesAcceptedEntriesAndRejectsInvalidOrUnauthorizedWrites, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition; FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
		F.Queue(F.A); F.Queue(F.A); F.Queue(F.A);
		auto Limit = Settings(ESeinProductionQueuePolicy::FixedAmountPerProductionUnit, 2);
		TestRunner->AddExpectedError(TEXT("SetProducerQueueSettings rejected outside bootstrap Applying"), EAutomationExpectedErrorFlags::Contains, 1, false);
		ASSERT_THAT(IsFalse(F.World->SetProducerQueueSettings(F.A, ItemClass(), &Limit)));
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		ASSERT_THAT(IsTrue(F.World->SetProducerQueueSettings(F.A, ItemClass(), &Limit)));
		ASSERT_THAT(AreEqual(3, F.Depth(F.A)));
		ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::LimitReached));
		Limit.QueueAmount = 0;
		ASSERT_THAT(IsFalse(F.World->SetProducerQueueSettings(F.A, ItemClass(), &Limit)));
		Limit.QueueAmount = 4;
		ASSERT_THAT(IsTrue(F.World->SetProducerQueueSettings(F.A, ItemClass(), &Limit)));
		ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::Available));
		Limit.QueuePolicy = static_cast<ESeinProductionQueuePolicy>(255);
		ASSERT_THAT(IsFalse(F.World->SetPlayerQueueSettings(Player, ItemClass(), &Limit)));
		ASSERT_THAT(IsTrue(F.World->SetProducerQueueSettings(F.A, ItemClass(), nullptr)));
	}

	TEST(OwnershipTransferCancelsPendingWithNormalRefunds, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition(ESeinProductionQueuePolicy::FixedAmountPerPlayer, 3, 10); FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize())); F.Queue(F.A, 20); F.Queue(F.A, 20);
		auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
		F.World->GetComponentMutable<FSeinProductionPayload>(F.A)->CurrentBuildProgress = FFixedPoint::FromInt(5);
		F.World->SetEntityOwner(F.A, OtherPlayer);
		ASSERT_THAT(AreEqual(0, F.Depth(F.A)));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(90).Value, F.World->GetPlayerState(Player)->GetResource(SeinARTSTags::Resource).Value));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(100).Value, F.World->GetPlayerState(OtherPlayer)->GetResource(SeinARTSTags::Resource).Value));
		ASSERT_THAT(IsTrue(F.Check(F.B) == ESeinProductionQueueResult::Available));
		ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::Available));
	}

	TEST(SameTickCommandsCannotDoubleSpendPlayerAllowance, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		TestRunner->AddExpectedError(TEXT("Component 'SeinAbilityPayload' has field(s) excluded from the legacy local state fingerprint"), EAutomationExpectedErrorFlags::Contains, 1, false);
		FDefinition Definition(ESeinProductionQueuePolicy::OncePerPlayer); FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize(true)));
		for (const auto Producer : {F.A, F.B})
			F.World->SubmitLocalCommandDraft(FSeinCommand::MakeAbilityCommand(Player, Producer, SeinARTSTags::Command_Context_AbilityTriggered));
		F.Tick();
		ASSERT_THAT(AreEqual(1, F.Depth(F.A) + F.Depth(F.B)));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(90).Value, F.World->GetPlayerState(Player)->GetResource(SeinARTSTags::Resource).Value));
	}

	TEST(ResearchCompletionKeepsAllowanceReservedDuringCallbacks, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition(ESeinProductionQueuePolicy::OncePerPlayer, 1, 0);
		auto& Item = Definition.Bridge->ComponentData[0].GetMutable<FSeinProduciblePayload>();
		Item.bIsResearch = true; Item.GrantedTechEffect = USeinEffectIdentityPlayerTestEffect::StaticClass();
		FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
		bool bObserved = false;
		USeinEffectMutationTestHook::Callback = [&](USeinEffectMutationTestHook&, FName Event, FSeinEntityHandle)
		{
			if (Event == GET_FUNCTION_NAME_CHECKED(USeinEffect, OnApply))
			{
				bObserved = true;
				ASSERT_THAT(IsTrue(F.Check(F.B) == ESeinProductionQueueResult::LimitReached));
				F.Queue(F.B);
			}
		};
		F.Queue(F.A); F.Tick();
		ASSERT_THAT(IsTrue(bObserved));
		ASSERT_THAT(AreEqual(0, F.Depth(F.B)));
		ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::LimitReached));
	}

	TEST(RejectedResearchCompletionRestoresReservationWithoutConsumingHistory, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition(ESeinProductionQueuePolicy::OncePerPlayer, 1, 0);
		auto& Item = Definition.Bridge->ComponentData[0].GetMutable<FSeinProduciblePayload>();
		Item.bIsResearch = true; Item.GrantedTechEffect = USeinEffectIdentityPlayerTestEffect::StaticClass();
		auto* Effect = GetMutableDefault<USeinEffectIdentityPlayerTestEffect>();
		TGuardValue<ESeinEffectStackingRule> Stacking(Effect->StackingRule, ESeinEffectStackingRule::Independent);
		TGuardValue<int32> MaxStacks(Effect->MaxStacks, 1);
		FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			F.World->ApplyEffectTransactional(F.A, Item.GrantedTechEffect, F.A);
		}
		F.Queue(F.A); F.Tick();
		ASSERT_THAT(AreEqual(1, F.Depth(F.A)));
		int64 Used;
		ASSERT_THAT(IsTrue(F.Check(F.B, &Used) == ESeinProductionQueueResult::LimitReached));
		ASSERT_THAT(AreEqual(int64(1), Used));
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			ASSERT_THAT(IsTrue(F.World->CancelProduction(F.A, 0)));
		}
		ASSERT_THAT(IsTrue(F.Check(F.B, &Used) == ESeinProductionQueueResult::Available));
		ASSERT_THAT(AreEqual(int64(0), Used));
	}

	TEST(InvalidResearchIsUnavailableAndEnqueueRefundsItsFunding, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition;
		Definition.Bridge->ComponentData[0].GetMutable<FSeinProduciblePayload>().bIsResearch = true;
		FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
		ASSERT_THAT(IsTrue(F.Check(F.A) == ESeinProductionQueueResult::InvalidProducible));
		F.Queue(F.A, 20);
		ASSERT_THAT(AreEqual(0, F.Depth(F.A)));
		ASSERT_THAT(AreEqual(FFixedPoint::FromInt(100).Value, F.World->GetPlayerState(Player)->GetResource(SeinARTSTags::Resource).Value));
	}

	TEST(ProducerDestructionReleasesPendingButPreservesCompletedPlayerAllowance, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition(ESeinProductionQueuePolicy::FixedAmountPerPlayer, 2, 0); FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize())); F.Queue(F.A); F.Tick(); F.Queue(F.B);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			F.World->DestroyEntity(F.A);
			F.World->DestroyEntity(F.B);
			F.World->SetEntityOwner(F.C, Player);
		}
		F.Tick();
		int64 Used;
		ASSERT_THAT(IsTrue(F.Check(F.C, &Used) == ESeinProductionQueueResult::Available));
		ASSERT_THAT(AreEqual(int64(1), Used));
	}

	TEST(SerialAndParallelProductionContinuationHaveEqualCanonicalRoots, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition(ESeinProductionQueuePolicy::FixedAmountPerPlayer, 2, 0);
		IConsoleVariable* Parallel = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.Parallel"));
		ASSERT_THAT(IsNotNull(Parallel));
		const int32 Previous = Parallel->GetInt();
		struct FRestore { IConsoleVariable* Variable; int32 Value; ~FRestore() { Variable->Set(Value, ECVF_SetByCode); } } Restore{Parallel, Previous};
		TArray<FGuid> Reference;
		for (int32 Mode = 0; Mode < 2; ++Mode)
		{
			Parallel->Set(Mode, ECVF_SetByCode);
			FFixture F; ASSERT_THAT(IsTrue(F.Initialize()));
			F.Queue(F.A); F.Queue(F.B);
			for (int32 Tick = 0; Tick < 3; ++Tick)
			{
				F.Tick(); FGuid Root; FString Error;
				ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(Root, Error)));
				if (Mode == 0) Reference.Add(Root);
				else ASSERT_THAT(IsTrue(Reference[Tick] == Root));
			}
		}
	}

	TEST(FreshRestorePreservesOverridesCompletionsAndFutureCancellation, "SeinARTS.Sim.ProductionPolicy")
	{
		using namespace ProductionPolicyTestLocal;
		FDefinition Definition(ESeinProductionQueuePolicy::MultiQueueable, 3, 0); FFixture F;
		ASSERT_THAT(IsTrue(F.Initialize())); F.Queue(F.A); F.Tick(); F.Queue(F.B);
		{
			auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
			auto Limit = Settings(ESeinProductionQueuePolicy::FixedAmountPerPlayer, 2);
			ASSERT_THAT(IsTrue(F.World->SetPlayerQueueSettings(Player, ItemClass(), &Limit)));
			auto Unit = Settings(ESeinProductionQueuePolicy::FixedAmountPerProductionUnit, 3);
			ASSERT_THAT(IsTrue(F.World->SetProducerQueueSettings(F.C, ItemClass(), &Unit)));
		}
		FSeinWorldSnapshot Snapshot; F.World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(AreEqual(FSeinWorldSnapshot::CurrentVersion, Snapshot.SnapshotVersion));
		FActorTestSpawner RestoredSpawner;
		auto* Restored = RestoredSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Restored, Snapshot)));
		for (auto* World : {F.World, Restored})
		{
			FSeinProductionQueueSettings Effective; int64 Used;
			ASSERT_THAT(IsTrue(World->CheckProductionQueue(F.A, ItemClass(), Effective, Used) == ESeinProductionQueueResult::LimitReached));
			ASSERT_THAT(AreEqual(int64(2), Used));
			auto Scope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(World->CancelProduction(F.B, 0)));
			ASSERT_THAT(IsTrue(World->CheckProductionQueue(F.A, ItemClass(), Effective, Used) == ESeinProductionQueueResult::Available));
		}
		for (int32 Tick = 0; Tick < 3; ++Tick)
		{
			F.Tick(); FGuid A, B; FString Error;
			ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(A, Error)));
			ASSERT_THAT(IsTrue(Restored->ComputeCanonicalStateRoot(B, Error)));
			ASSERT_THAT(IsTrue(A == B));
		}
		Restored->StopSimulation();
	}
}
