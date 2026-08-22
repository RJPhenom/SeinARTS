/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         EconomyLoopTests.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Qualifies designer-composed harvest, dropoff, and worker
 *               construction loops through commands and snapshot continuation.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinEntityComponent.h"
#include "Components/SeinAbilityComponent.h"
#include "Components/SeinConstructionComponent.h"
#include "Core/SeinPlayerState.h"
#include "Data/SeinWorldSnapshot.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Lib/SeinConstructionBPFL.h"
#include "Lib/SeinResourceBPFL.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinEconomyLoopTestTypes.h"

struct FSeinWorldSubsystemTestAccess
{
	static bool TickSimulation(USeinWorldSubsystem& World, float DeltaTime)
	{
		return World.TickSimulation(DeltaTime);
	}
};

namespace UE::SeinARTSTests
{
	namespace EconomyLoopTestLocal
	{
		struct FScopedResourceCatalog
		{
			FScopedResourceCatalog()
			{
				Settings = GetMutableDefault<USeinARTSCoreSettings>();
				check(Settings);
				PreviousCatalog = Settings->ResourceCatalog;

				FSeinResourceDefinition Definition;
				Definition.ResourceTag = SeinARTSTags::Resource;
				Definition.DefaultStartingValue = FFixedPoint::Zero;
				Definition.CostDirection =
					ESeinCostDirection::DeductFromBalance;
				Definition.SpendBehavior =
					ESeinResourceSpendBehavior::RejectOnInsufficient;
				Settings->ResourceCatalog = {Definition};
			}

			~FScopedResourceCatalog()
			{
				Settings->ResourceCatalog = MoveTemp(PreviousCatalog);
			}

			USeinARTSCoreSettings* Settings = nullptr;
			TArray<FSeinResourceDefinition> PreviousCatalog;
		};

		struct FScopedConstructionActor
		{
			explicit FScopedConstructionActor(bool bAuthorPersistentTag)
			{
				TArray<const USeinEntityComponent*> Bridges;
				AActor::GetActorClassDefaultComponents<USeinEntityComponent>(
					ASeinEconomyConstructionTestActor::StaticClass(), Bridges);
				check(!Bridges.IsEmpty());
				Bridge = const_cast<USeinEntityComponent*>(Bridges[0]);
				PreviousComponentData = Bridge->ComponentData;
				PreviousBaseTags = Bridge->BaseTags;

				Bridge->ComponentData.Reset();
				FSeinConstructionComponent Construction;
				Construction.TimeToCompletion = FFixedPoint::FromInt(3);
				Bridge->ComponentData.Add(
					FInstancedStruct::Make(Construction));
				Bridge->BaseTags.Reset();
				if (bAuthorPersistentTag)
				{
					Bridge->BaseTags.AddTag(
						SeinARTSTags::State_UnderConstruction);
				}
			}

			~FScopedConstructionActor()
			{
				Bridge->ComponentData = MoveTemp(PreviousComponentData);
				Bridge->BaseTags = MoveTemp(PreviousBaseTags);
			}

			USeinEntityComponent* Bridge = nullptr;
			TArray<FInstancedStruct> PreviousComponentData;
			FGameplayTagContainer PreviousBaseTags;
		};

		void Tick(USeinWorldSubsystem& World, int32 Count = 1)
		{
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FSeinWorldSubsystemTestAccess::TickSimulation(
					World, World.GetFixedDeltaTimeSeconds());
			}
		}

		USeinAbility* GrantAbility(
			USeinWorldSubsystem& World,
			FSeinEntityHandle Owner,
			TSubclassOf<USeinAbility> AbilityClass,
			FGameplayTag AbilityTag)
		{
			const int32 AbilityID = USeinAbilityBPFL::SeinGrantAbility(
				&World, Owner, AbilityClass);
			USeinAbility* Ability = World.GetAbilityInstance(AbilityID);
			if (Ability)
			{
				Ability->AbilityTag = AbilityTag;
			}
			return Ability;
		}

		void SubmitAbility(
			USeinWorldSubsystem& World,
			FSeinPlayerID Player,
			FSeinEntityHandle Owner,
			FGameplayTag AbilityTag,
			FSeinEntityHandle Target)
		{
			World.SubmitLocalCommandDraft(FSeinCommand::MakeAbilityCommand(
				Player, Owner, AbilityTag, Target));
		}

		FFixedPoint ResourceValue(
			const USeinWorldSubsystem& World, FSeinPlayerID Player)
		{
			const FSeinPlayerState* State = World.GetPlayerState(Player);
			return State
				? State->GetResource(SeinARTSTags::Resource)
				: FFixedPoint::MinValue;
		}
	}

	TEST(HarvestAndDropoffAbilitiesRoundTripCanonicalState,
		"SeinARTS.Sim.Economy.Loops")
	{
		using namespace EconomyLoopTestLocal;
		FScopedResourceCatalog Catalog;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		FSeinEntityHandle Worker;
		FSeinEntityHandle Node;
		FSeinEntityHandle Dropoff;
		const FGameplayTag GatherTag =
			SeinARTSTags::Command_Context_Target_Neutral;
		const FGameplayTag DropoffTag =
			SeinARTSTags::Command_Context_Target_Friendly;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));
				Worker = World->SpawnAbstractEntity(FFixedTransform(), Player);
				Node = World->SpawnAbstractEntity(
					FFixedTransform(), FSeinPlayerID::Neutral());
				Dropoff = World->SpawnAbstractEntity(FFixedTransform(), Player);

				World->AddComponent(Worker, FSeinAbilityComponent());
				World->AddComponent(Worker, FSeinEconomyCargoTestComponent());
				FSeinEconomyResourceNodeTestComponent ResourceNode;
				ResourceNode.Available = FFixedPoint::FromInt(25);
				World->AddComponent(Node, ResourceNode);
				World->AddComponent(
					Dropoff, FSeinEconomyDropoffTestComponent());

				GrantAbility(
					*World,
					Worker,
					USeinEconomyGatherTestAbility::StaticClass(),
					GatherTag);
				GrantAbility(
					*World,
					Worker,
					USeinEconomyDropoffTestAbility::StaticClass(),
					DropoffTag);
			})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		SubmitAbility(*World, Player, Worker, GatherTag, Node);
		Tick(*World);
		const FSeinEconomyResourceNodeTestComponent* NodeState =
			World->GetComponent<FSeinEconomyResourceNodeTestComponent>(Node);
		const FSeinEconomyCargoTestComponent* Cargo =
			World->GetComponent<FSeinEconomyCargoTestComponent>(Worker);
		ASSERT_THAT(IsNotNull(NodeState));
		ASSERT_THAT(IsNotNull(Cargo));
		ASSERT_THAT(IsTrue(
			NodeState->Available == FFixedPoint::FromInt(15)));
		ASSERT_THAT(IsTrue(Cargo->Amount == FFixedPoint::FromInt(10)));
		ASSERT_THAT(IsTrue(ResourceValue(*World, Player) == FFixedPoint::Zero));

		FSeinWorldSnapshot HarvestedSnapshot;
		World->CaptureSnapshot(HarvestedSnapshot);
		SubmitAbility(*World, Player, Worker, DropoffTag, Dropoff);
		Tick(*World);
		Cargo = World->GetComponent<FSeinEconomyCargoTestComponent>(Worker);
		ASSERT_THAT(IsNotNull(Cargo));
		ASSERT_THAT(IsTrue(Cargo->Amount == FFixedPoint::Zero));
		ASSERT_THAT(IsTrue(
			ResourceValue(*World, Player) == FFixedPoint::FromInt(10)));
		FGuid FirstFinalRoot;
		FString RootError;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			FirstFinalRoot, RootError)));
		FSeinWorldSnapshot CompletedSnapshot;
		World->CaptureSnapshot(CompletedSnapshot);

		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, HarvestedSnapshot)));
		Cargo = World->GetComponent<FSeinEconomyCargoTestComponent>(Worker);
		NodeState =
			World->GetComponent<FSeinEconomyResourceNodeTestComponent>(Node);
		ASSERT_THAT(IsNotNull(Cargo));
		ASSERT_THAT(IsNotNull(NodeState));
		ASSERT_THAT(IsTrue(Cargo->Amount == FFixedPoint::FromInt(10)));
		ASSERT_THAT(IsTrue(
			NodeState->Available == FFixedPoint::FromInt(15)));
		ASSERT_THAT(IsTrue(ResourceValue(*World, Player) == FFixedPoint::Zero));

		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, CompletedSnapshot)));
		FGuid RestoredFinalRoot;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			RestoredFinalRoot, RootError)));
		ASSERT_THAT(IsTrue(FirstFinalRoot == RestoredFinalRoot));
		World->StopSimulation();
	}

	TEST(WorkerConstructionContinuesExactlyAndLeavesRestorableTagState,
		"SeinARTS.Sim.Economy.Construction")
	{
		using namespace EconomyLoopTestLocal;
		FScopedConstructionActor ConstructionActor(false);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		const FGameplayTag ConstructTag =
			SeinARTSTags::Command_Context_Target_Friendly;
		FSeinEntityHandle Worker;
		FSeinEntityHandle Building;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));
				Worker = World->SpawnAbstractEntity(FFixedTransform(), Player);
				World->AddComponent(Worker, FSeinAbilityComponent());
				GrantAbility(
					*World,
					Worker,
					USeinEconomyConstructTestAbility::StaticClass(),
					ConstructTag);
				Building = World->SpawnEntity(
					ASeinEconomyConstructionTestActor::StaticClass(),
					FFixedTransform(),
					Player);
			})));
		ASSERT_THAT(IsTrue(Building.IsValid()));
		ASSERT_THAT(IsTrue(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));
		ASSERT_THAT(IsFalse(World->GetEntityBaseTags(Building).HasTagExact(
			SeinARTSTags::State_UnderConstruction)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		SubmitAbility(*World, Player, Worker, ConstructTag, Building);
		Tick(*World);
		const FSeinConstructionComponent* Construction =
			World->GetComponent<FSeinConstructionComponent>(Building);
		ASSERT_THAT(IsNotNull(Construction));
		ASSERT_THAT(IsTrue(Construction->Progress > FFixedPoint::Zero));

		FSeinWorldSnapshot PartialSnapshot;
		World->CaptureSnapshot(PartialSnapshot);
		int32 ContinuationTicks = 0;
		while (World->GetComponent<FSeinConstructionComponent>(Building)
			&& ContinuationTicks < 10)
		{
			Tick(*World);
			++ContinuationTicks;
		}
		ASSERT_THAT(IsTrue(ContinuationTicks > 0 && ContinuationTicks < 10));
		ASSERT_THAT(IsFalse(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));
		FGuid FirstFinalRoot;
		FString RootError;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			FirstFinalRoot, RootError)));

		FSeinWorldSnapshot CompletedSnapshot;
		World->CaptureSnapshot(CompletedSnapshot);
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, CompletedSnapshot)));
		ASSERT_THAT(IsFalse(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));

		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, PartialSnapshot)));
		Tick(*World, ContinuationTicks);
		FGuid RestoredFinalRoot;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			RestoredFinalRoot, RootError)));
		ASSERT_THAT(IsTrue(FirstFinalRoot == RestoredFinalRoot));
		World->StopSimulation();
	}

	TEST(ConstructionCompletionPreservesDesignerAuthoredBaseGrant,
		"SeinARTS.Sim.Economy.Construction")
	{
		using namespace EconomyLoopTestLocal;
		FScopedConstructionActor ConstructionActor(true);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle Building;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				const FSeinPlayerID Player(1);
				World->RegisterPlayer(Player, FSeinFactionID(1));
				Building = World->SpawnEntity(
					ASeinEconomyConstructionTestActor::StaticClass(),
					FFixedTransform(),
					Player);
			})));
		ASSERT_THAT(IsTrue(Building.IsValid()));
		ASSERT_THAT(IsTrue(World->GetEntityBaseTags(Building).HasTagExact(
			SeinARTSTags::State_UnderConstruction)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			USeinConstructionBPFL::SeinFinishConstruction(World, Building);
		}
		ASSERT_THAT(IsNull(
			World->GetComponent<FSeinConstructionComponent>(Building)));
		ASSERT_THAT(IsTrue(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));
		ASSERT_THAT(IsTrue(World->GetEntityBaseTags(Building).HasTagExact(
			SeinARTSTags::State_UnderConstruction)));

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, Snapshot)));
		ASSERT_THAT(IsTrue(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));
		World->StopSimulation();
	}

	TEST(PlacedConstructionActorReleasesOnlyItsFrameworkTagGrant,
		"SeinARTS.Sim.Economy.Construction")
	{
		using namespace EconomyLoopTestLocal;
		FScopedConstructionActor ConstructionActor(false);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		ASeinEconomyConstructionTestActor& PlacedActor =
			Spawner.SpawnActor<ASeinEconomyConstructionTestActor>();
		PlacedActor.PlacedSimLocation = FFixedVector::ZeroVector;
		PlacedActor.PlacedSimRotation = FFixedQuaternion::Identity;
		PlacedActor.bSimLocationBaked = true;
		PlacedActor.bSimRotationBaked = true;
		USeinEntityComponent* LiveBridge =
			PlacedActor.FindComponentByClass<USeinEntityComponent>();
		ASSERT_THAT(IsNotNull(LiveBridge));
		LiveBridge->ComponentData.Reset();
		FSeinConstructionComponent PlacedConstruction;
		PlacedConstruction.TimeToCompletion = FFixedPoint::FromInt(3);
		LiveBridge->ComponentData.Add(
			FInstancedStruct::Make(PlacedConstruction));
		LiveBridge->BaseTags.Reset();

		FSeinEntityHandle Building;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				const FSeinPlayerID Player(1);
				World->RegisterPlayer(Player, FSeinFactionID(1));
				Building = World->SpawnEntityFromPlacedActor(
					&PlacedActor, Player);
			})));
		ASSERT_THAT(IsTrue(Building.IsValid()));
		ASSERT_THAT(IsTrue(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));
		ASSERT_THAT(IsFalse(World->GetEntityBaseTags(Building).HasTagExact(
			SeinARTSTags::State_UnderConstruction)));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			USeinConstructionBPFL::SeinFinishConstruction(World, Building);
		}
		ASSERT_THAT(IsFalse(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));

		FSeinWorldSnapshot Snapshot;
		World->CaptureSnapshot(Snapshot);
		ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(
			*World, Snapshot)));
		ASSERT_THAT(IsFalse(World->HasTag(
			Building, SeinARTSTags::State_UnderConstruction)));
		World->StopSimulation();
	}

	TEST(ConstructionProgressRejectsInvalidArithmeticAndCompletesZeroTime,
		"SeinARTS.Sim.Economy.Construction")
	{
		using namespace EconomyLoopTestLocal;
		FScopedConstructionActor ConstructionActor(false);
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		FSeinEntityHandle BoundedBuilding;
		FSeinEntityHandle ZeroTimeBuilding;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				const FSeinPlayerID Player(1);
				World->RegisterPlayer(Player, FSeinFactionID(1));
				BoundedBuilding = World->SpawnEntity(
					ASeinEconomyConstructionTestActor::StaticClass(),
					FFixedTransform(),
					Player);
				ZeroTimeBuilding = World->SpawnEntity(
					ASeinEconomyConstructionTestActor::StaticClass(),
					FFixedTransform(),
					Player);
				FSeinConstructionComponent* ZeroTime =
					World->GetComponentMutable<FSeinConstructionComponent>(
						ZeroTimeBuilding);
				check(ZeroTime);
				ZeroTime->TimeToCompletion = FFixedPoint::Zero;
			})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FGuid InitialRoot;
		FString RootError;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			InitialRoot, RootError)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsFalse(
				USeinConstructionBPFL::SeinAddConstructionProgress(
					World, BoundedBuilding, FFixedPoint::Zero)));
			ASSERT_THAT(IsFalse(
				USeinConstructionBPFL::SeinAddConstructionProgress(
					World, BoundedBuilding, -FFixedPoint::SmallNumber)));
		}
		FGuid RejectedRoot;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			RejectedRoot, RootError)));
		ASSERT_THAT(IsTrue(InitialRoot == RejectedRoot));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			FSeinConstructionComponent* Construction =
				World->GetComponentMutable<FSeinConstructionComponent>(
					BoundedBuilding);
			ASSERT_THAT(IsNotNull(Construction));
			Construction->Progress = FFixedPoint(MAX_int64 - 1);
			Construction->TimeToCompletion = FFixedPoint::MaxValue;
		}
		FGuid PreOverflowRoot;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			PreOverflowRoot, RootError)));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsFalse(
				USeinConstructionBPFL::SeinAddConstructionProgress(
					World, BoundedBuilding, FFixedPoint(2))));
		}
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			RejectedRoot, RootError)));
		ASSERT_THAT(IsTrue(PreOverflowRoot == RejectedRoot));

		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			ASSERT_THAT(IsTrue(
				USeinConstructionBPFL::SeinAddConstructionProgress(
					World, ZeroTimeBuilding, FFixedPoint::SmallNumber)));
		}
		ASSERT_THAT(IsNull(
			World->GetComponent<FSeinConstructionComponent>(ZeroTimeBuilding)));
		ASSERT_THAT(IsFalse(World->HasTag(
			ZeroTimeBuilding, SeinARTSTags::State_UnderConstruction)));
		World->StopSimulation();
	}

	TEST(IncomeGrantRejectsMalformedMapsAndSaturatesNumericOverflow,
		"SeinARTS.Sim.Economy.Resources")
	{
		using namespace EconomyLoopTestLocal;
		FScopedResourceCatalog Catalog;
		FActorTestSpawner Spawner;
		USeinWorldSubsystem* World =
			Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));

		const FSeinPlayerID Player(1);
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(
			*World,
			[&]()
			{
				World->RegisterPlayer(Player, FSeinFactionID(1));
				FSeinPlayerState* State = World->GetPlayerStateMutable(Player);
				check(State);
				State->Resources.Add(
					SeinARTSTags::Resource, FFixedPoint(MAX_int64 - 1));
			})));
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*World)));

		FGuid InitialRoot;
		FString RootError;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			InitialRoot, RootError)));
		FSeinResourceCost Malformed;
		Malformed.Amounts.Add(SeinARTSTags::Resource, FFixedPoint(1));
		Malformed.Amounts.Add(FGameplayTag(), FFixedPoint(1));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			USeinResourceBPFL::SeinGrantIncome(World, Player, Malformed);
		}
		FGuid RejectedRoot;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			RejectedRoot, RootError)));
		ASSERT_THAT(IsTrue(InitialRoot == RejectedRoot));

		FSeinResourceCost Negative;
		Negative.Amounts.Add(
			SeinARTSTags::Resource, -FFixedPoint::SmallNumber);
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			USeinResourceBPFL::SeinGrantIncome(World, Player, Negative);
		}
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			RejectedRoot, RootError)));
		ASSERT_THAT(IsTrue(InitialRoot == RejectedRoot));

		FSeinResourceCost Overflow;
		Overflow.Amounts.Add(SeinARTSTags::Resource, FFixedPoint(2));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			USeinResourceBPFL::SeinGrantIncome(World, Player, Overflow);
		}
		ASSERT_THAT(IsTrue(
			ResourceValue(*World, Player) == FFixedPoint::MaxValue));
		FGuid MaximumRoot;
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			MaximumRoot, RootError)));

		FSeinResourceCost AtMaximum;
		AtMaximum.Amounts.Add(SeinARTSTags::Resource, FFixedPoint(1));
		{
			auto SimScope = FSeinSimContextTestAccess::Enter(*World);
			USeinResourceBPFL::SeinGrantIncome(World, Player, AtMaximum);
		}
		ASSERT_THAT(IsTrue(World->ComputeCanonicalStateRoot(
			RejectedRoot, RootError)));
		ASSERT_THAT(IsTrue(MaximumRoot == RejectedRoot));
		World->StopSimulation();
	}
}
