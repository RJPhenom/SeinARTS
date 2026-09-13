/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         DefaultMoveAbilityTests.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Explicit movement selection, grant lifecycle, and snapshot continuation.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "CQTest.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SeinAbilityPayload.h"
#include "Components/SeinMovementPayload.h"
#include "Containers/Ticker.h"
#include "Data/SeinWorldSnapshot.h"
#include "Input/SeinCommand.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSimContext.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinARTSGameplayTags.h"
#include "TestTypes/SeinProductionCostTestTypes.h"

namespace
{
    struct FMoveFixture
    {
        FActorTestSpawner Spawner;
        USeinWorldSubsystem* World = Spawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
        FSeinEntityHandle Entity;
        int32 FirstID = INDEX_NONE;
        int32 SecondID = INDEX_NONE;

        bool Initialize()
        {
            return SeinTestMatchBootstrap::Materialize(*World, [&]
            {
                Entity = World->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID::Neutral());
                World->AddComponent(Entity, FSeinAbilityPayload());
                FirstID = USeinAbilityBPFL::SeinGrantAbility(World, Entity, USeinDefaultMoveTestFirstAbility::StaticClass());
                SecondID = USeinAbilityBPFL::SeinGrantAbility(World, Entity, USeinDefaultMoveTestSelectedAbility::StaticClass());
                for (int32 ID : {FirstID, SecondID})
                    if (auto* Ability = World->GetAbilityInstance(ID))
                        Ability->TargetType = ESeinAbilityTargetType::Point;
                World->GetAbilityInstance(FirstID)->AbilityTag = SeinARTSTags::Command_Context_RightClick;
                World->GetAbilityInstance(SecondID)->AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
            }) && SeinTestMatchBootstrap::Start(*World);
        }

        void Select(UClass* Class)
        {
            auto Scope = FSeinSimContextTestAccess::Enter(*World);
            FSeinMovementPayload Movement;
            Movement.DefaultMoveAbility = Class;
            World->AddComponent(Entity, Movement);
        }
    };
}

TEST(SelectsExactGrantWithoutFallbackAndTracksRevocation, "SeinARTS.Sim.Abilities.DefaultMove")
{
    FMoveFixture F;
    ASSERT_THAT(IsTrue(F.Initialize()));
    ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
    F.Select(nullptr);
    ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
    F.Select(USeinDefaultMoveTestSelectedAbility::StaticClass());
    ASSERT_THAT(IsTrue(F.World->ResolveDefaultMoveAbility(F.Entity) == F.World->GetAbilityInstance(F.SecondID)));
    {
        auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
        ASSERT_THAT(AreEqual(1, USeinAbilityBPFL::SeinForceRevokeAbilityByClass(
            F.World, F.Entity, USeinDefaultMoveTestSelectedAbility::StaticClass())));
        ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
        F.SecondID = USeinAbilityBPFL::SeinGrantAbility(F.World, F.Entity, USeinDefaultMoveTestSelectedAbility::StaticClass());
        auto* Second = F.World->GetAbilityInstance(F.SecondID);
        ASSERT_THAT(IsNotNull(Second));
        Second->TargetType = ESeinAbilityTargetType::Point;
        Second->AbilityTag = SeinARTSTags::Command_Context_Target_Ground;
    }
    ASSERT_THAT(IsTrue(F.World->ResolveDefaultMoveAbility(F.Entity) == F.World->GetAbilityInstance(F.SecondID)));
}

TEST(RejectsIneligibleAndAmbiguousGrants, "SeinARTS.Sim.Abilities.DefaultMove")
{
    FMoveFixture F;
    ASSERT_THAT(IsTrue(F.Initialize()));
    F.Select(USeinDefaultMoveTestSelectedAbility::StaticClass());
    auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
    auto* Second = F.World->GetAbilityInstance(F.SecondID);
    Second->bIsPassive = true;
    ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
    Second->bIsPassive = false;
    Second->TargetType = ESeinAbilityTargetType::None;
    ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
    Second->TargetType = ESeinAbilityTargetType::Point;
    Second->AbilityTag = FGameplayTag();
    ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
    Second->AbilityTag = F.World->GetAbilityInstance(F.FirstID)->AbilityTag;
    ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
    F.World->GetComponentMutable<FSeinMovementPayload>(F.Entity)->DefaultMoveAbility = USeinDefaultMoveTestFirstAbility::StaticClass();
    ASSERT_THAT(IsNull(F.World->ResolveDefaultMoveAbility(F.Entity)));
}

TEST(SelectionSurvivesFreshWorldRestoreAndFutureTicks, "SeinARTS.Determinism.Abilities.DefaultMove")
{
    FMoveFixture F;
    ASSERT_THAT(IsTrue(F.Initialize()));
    F.Select(USeinDefaultMoveTestSelectedAbility::StaticClass());
    FGuid SelectedRoot, OtherRoot;
    FString Error;
    ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(SelectedRoot, Error)));
    {
        auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
        F.World->GetComponentMutable<FSeinMovementPayload>(F.Entity)->DefaultMoveAbility = USeinDefaultMoveTestFirstAbility::StaticClass();
    }
    ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(OtherRoot, Error)));
    ASSERT_THAT(IsTrue(SelectedRoot != OtherRoot));
    {
        auto Scope = FSeinSimContextTestAccess::Enter(*F.World);
        F.World->GetComponentMutable<FSeinMovementPayload>(F.Entity)->DefaultMoveAbility = USeinDefaultMoveTestSelectedAbility::StaticClass();
    }
    FSeinWorldSnapshot Snapshot;
    F.World->CaptureSnapshot(Snapshot);
    FActorTestSpawner RestoredSpawner;
    auto* Restored = RestoredSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
    ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Restored, Snapshot)));
    for (int32 Tick = 0; Tick < 4; ++Tick)
    {
        // Change the default identically after restore; new resolution follows
        // the changed state and both worlds must continue with the same root.
        if (Tick == 1)
            for (auto* World : {F.World, Restored})
            {
                auto Scope = FSeinSimContextTestAccess::Enter(*World);
                World->GetComponentMutable<FSeinMovementPayload>(F.Entity)->DefaultMoveAbility = USeinDefaultMoveTestFirstAbility::StaticClass();
            }
        FTSTicker::GetCoreTicker().Tick(F.World->GetFixedDeltaTimeSeconds());
        ASSERT_THAT(AreEqual(F.World->GetCurrentTick(), Restored->GetCurrentTick()));
        ASSERT_THAT(IsTrue(F.World->ComputeCanonicalStateRoot(SelectedRoot, Error)));
        ASSERT_THAT(IsTrue(Restored->ComputeCanonicalStateRoot(OtherRoot, Error)));
        ASSERT_THAT(IsTrue(SelectedRoot == OtherRoot));
        const auto* Resolved = Restored->ResolveDefaultMoveAbility(F.Entity);
        ASSERT_THAT(IsNotNull(Resolved));
        ASSERT_THAT(IsTrue(Resolved->GetClass() ==
            (Tick == 0 ? USeinDefaultMoveTestSelectedAbility::StaticClass() : USeinDefaultMoveTestFirstAbility::StaticClass())));
    }
}
