#include "CQTest.h"
#include "Components/ActorTestSpawner.h"

#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinIdentityPayload.h"
#include "Components/SeinProductionPayload.h"
#include "Engine/Texture2D.h"
#include "Templates/UnrealTemplate.h"
#include "Serialization/SeinCanonicalReflectedStateDigest.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinProductionCostTestTypes.h"
#include "ViewModel/SeinEntityViewModel.h"

namespace UE::SeinARTSTests
{
	TEST(QueueUsesSelectedClassIdentityAndProducerHandleForUnitsAndResearch,
		"SeinARTS.Unit.UI.ProductionQueue")
	{
		TArray<const USeinEntityBridgeComponent*> Bridges;
		AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(
			ASeinProductionCostTestActor::StaticClass(), Bridges);
		ASSERT_THAT(IsFalse(Bridges.IsEmpty()));
		auto* Bridge = const_cast<USeinEntityBridgeComponent*>(Bridges[0]);
		TGuardValue<TArray<FInstancedStruct>> RestoreData(Bridge->ComponentData, {});
		FSeinIdentityPayload Identity;
		Identity.DisplayName = FText::FromString(TEXT("Queued definition"));
		Identity.IdentityTag = SeinARTSTags::Resource;
		Identity.Icon = NewObject<UTexture2D>();
		Bridge->ComponentData.Add(FInstancedStruct::Make(Identity));

		auto* Effect = GetMutableDefault<USeinProductionCostTestResearchEffect>();
		TGuardValue<FGameplayTag> RestoreTag(Effect->EffectTag, SeinARTSTags::State_UnderConstruction);
		TGuardValue<FText> RestoreName(Effect->DisplayName, FText::FromString(TEXT("Different effect")));
		TGuardValue<TObjectPtr<UTexture2D>> RestoreIcon(Effect->Icon, NewObject<UTexture2D>());

		FActorTestSpawner Spawner;
		auto* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
		ASSERT_THAT(IsNotNull(World));
		FSeinEntityHandle Producer;
		FSeinEntityHandle OtherProducer;
		ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*World, [&]
		{
			const FSeinPlayerID Player(1);
			World->RegisterPlayer(Player, FSeinFactionID(1));
			Producer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			OtherProducer = World->SpawnAbstractEntity(FFixedTransform(), Player);
			FSeinProductionPayload Production;
			FSeinProductionQueueEntry Entry;
			Entry.ActorClass = ASeinProductionCostTestActor::StaticClass();
			Entry.TotalBuildTime = FFixedPoint::FromInt(10);
			Production.Queue.Add(Entry);
			Entry.bIsResearch = true;
			Entry.ResearchEffectClass = Effect->GetClass();
			Production.Queue.Add(Entry);
			Production.CurrentBuildProgress = FFixedPoint::FromInt(5);
			World->AddComponent(Producer, Production);
			World->AddComponent(OtherProducer, Production);
		})));

		auto* ViewModel = NewObject<USeinEntityViewModel>();
		for (const FSeinEntityHandle Owner : {Producer, OtherProducer})
		{
			ViewModel->Initialize(Owner, World);
			const auto Items = ViewModel->GetProductionQueue();
			ASSERT_THAT(AreEqual(2, Items.Num()));
			for (int32 Index = 0; Index < Items.Num(); ++Index)
			{
				ASSERT_THAT(IsTrue(Items[Index].QueueOwner == Owner));
				ASSERT_THAT(AreEqual(Index, Items[Index].QueueIndex));
				ASSERT_THAT(IsTrue(Items[Index].DisplayName.EqualTo(Identity.DisplayName)));
				ASSERT_THAT(IsTrue(Items[Index].Icon == Identity.Icon));
				ASSERT_THAT(IsTrue(Items[Index].IdentityTag == Identity.IdentityTag));
			}
			ASSERT_THAT(IsFalse(Items[0].bIsResearch));
			ASSERT_THAT(IsTrue(Items[1].bIsResearch));
			ASSERT_THAT(IsNear(0.5f, Items[0].ProgressPercent, 0.0001f));
			ASSERT_THAT(IsNear(0.0f, Items[1].ProgressPercent, 0.0001f));
		}

		// Missing entity identity must not silently substitute the research effect.
		Bridge->ComponentData.Reset();
		const auto MissingIdentity = ViewModel->GetProductionQueue();
		ASSERT_THAT(AreEqual(2, MissingIdentity.Num()));
		ASSERT_THAT(IsTrue(MissingIdentity[1].DisplayName.IsEmpty()));
		ASSERT_THAT(IsTrue(MissingIdentity[1].Icon == nullptr));
		ASSERT_THAT(IsFalse(MissingIdentity[1].IdentityTag.IsValid()));
		ASSERT_THAT(IsTrue(MissingIdentity[1].QueueOwner == OtherProducer));
	}

	TEST(EffectPresentationDoesNotChangeCanonicalDefinitionDigest,
		"SeinARTS.Unit.UI.ProductionQueue")
	{
		auto* Effect = NewObject<USeinProductionCostTestResearchEffect>();
		FSeinCanonicalReflectedStateLimits Limits;
		FGuid Schema;
		FGuid Before;
		FGuid After;
		FString Error;
		ASSERT_THAT(IsTrue(FSeinCanonicalReflectedStateDigest::ComputeSchemaDigest(
			Effect->GetClass(), Limits, Schema, Error)));
		ASSERT_THAT(IsTrue(FSeinCanonicalReflectedStateDigest::ComputeObjectValueDigest(
			Effect, Schema, Limits, Before, Error)));
		Effect->DisplayName = FText::FromString(TEXT("Factory access"));
		Effect->Description = FText::FromString(TEXT("Allows factory construction."));
		Effect->Icon = NewObject<UTexture2D>();
		ASSERT_THAT(IsTrue(FSeinCanonicalReflectedStateDigest::ComputeObjectValueDigest(
			Effect, Schema, Limits, After, Error)));
		ASSERT_THAT(IsTrue(Before == After));
		Effect->Duration = Effect->Duration + FFixedPoint::One;
		ASSERT_THAT(IsTrue(FSeinCanonicalReflectedStateDigest::ComputeObjectValueDigest(
			Effect, Schema, Limits, After, Error)));
		ASSERT_THAT(IsFalse(Before == After));
	}
}
