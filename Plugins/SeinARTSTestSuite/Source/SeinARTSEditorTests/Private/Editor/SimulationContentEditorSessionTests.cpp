/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SimulationContentEditorSessionTests.cpp
 * @author       RJ Macklem
 * @created      6 Sep 2026
 * @latest       6 Sep 2026
 * @brief        Verifies unsaved Blueprint admission and snapshot continuity in ordinary Play.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "CQTest.h"
#include "Abilities/SeinAbility.h"
#include "Authoring/SeinVisionComponent.h"
#include "Components/ActorTestSpawner.h"
#include "Components/SeinAbilityPayload.h"
#include "Data/SeinWorldSnapshot.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Lib/SeinAbilityBPFL.h"
#include "Misc/ScopeExit.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinTestMatchBootstrap.h"
#include "Simulation/SeinTestSnapshotRestore.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

TEST(UnsavedBlueprintAbilitySurvivesSnapshotAndFutureTicks,
    "SeinARTS.Editor.SimulationContent")
{
    IConsoleVariable* Strict = IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.SimulationContent.RequireFreshManifestForPIE"));
    const int32 Previous = Strict->GetInt();
    Strict->Set(0, ECVF_SetByCode);
    ON_SCOPE_EXIT { Strict->Set(Previous, ECVF_SetByCode); };
    UPackage* Package = CreatePackage(TEXT("/SeinARTSTestSuite/UnsavedSessionAbility"));
    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(USeinAbility::StaticClass(), Package,
        TEXT("UnsavedSessionAbility"), BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
    ASSERT_THAT(IsNotNull(Blueprint));
    ON_SCOPE_EXIT
    {
        Blueprint->ClearFlags(RF_Public | RF_Standalone);
        Blueprint->SetFlags(RF_Transient);
        Package->SetDirtyFlag(false);
    };
    FEdGraphPinType Type;
    Type.PinCategory = UEdGraphSchema_K2::PC_Int;
    ASSERT_THAT(IsTrue(FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Counter"), Type)));
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    ASSERT_THAT(IsTrue(Blueprint->IsUpToDate()));
    ASSERT_THAT(IsTrue(Package->IsDirty()));
    FIntProperty* Counter = FindFProperty<FIntProperty>(Blueprint->GeneratedClass, TEXT("Counter"));
    ASSERT_THAT(IsNotNull(Counter));
    FActorTestSpawner SourceSpawner;
    USeinWorldSubsystem* Source = SourceSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
    ASSERT_THAT(IsTrue(Source->IsSimulationContentReady()));
    ASSERT_THAT(IsTrue(Source->IsSimulationContentSynthesized()));
    int32 AbilityId = INDEX_NONE;
    ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Materialize(*Source, [&]
    {
        const auto Entity = Source->SpawnAbstractEntity(FFixedTransform(), FSeinPlayerID::Neutral());
        Source->AddComponent(Entity, FSeinAbilityPayload());
        AbilityId = USeinAbilityBPFL::SeinGrantAbility(Source, Entity, Blueprint->GeneratedClass.Get());
        if (USeinAbility* Ability = Source->GetAbilityInstance(AbilityId))
            Counter->SetPropertyValue_InContainer(Ability, 42);
    })));
    ASSERT_THAT(IsTrue(AbilityId != INDEX_NONE));
    ASSERT_THAT(IsTrue(SeinTestMatchBootstrap::Start(*Source)));
    FSeinWorldSnapshot Snapshot;
    Source->CaptureSnapshot(Snapshot);
    ASSERT_THAT(AreEqual(1, Snapshot.AbilityPoolRecords.Num()));
    FActorTestSpawner RestoredSpawner;
    USeinWorldSubsystem* Restored = RestoredSpawner.GetWorld().GetSubsystem<USeinWorldSubsystem>();
    ASSERT_THAT(IsTrue(SeinTestSnapshotRestore::RestoreTrusted(*Restored, Snapshot)));
    ASSERT_THAT(AreEqual(42, Counter->GetPropertyValue_InContainer(Restored->GetAbilityInstance(AbilityId))));
    ASSERT_THAT(IsTrue(Restored->IsSimulationRunning()));
    for (int32 Tick = 0; Tick < 4; ++Tick)
    {
        FTSTicker::GetCoreTicker().Tick(Source->GetFixedDeltaTimeSeconds());
        ASSERT_THAT(AreEqual(Source->GetCurrentTick(), Restored->GetCurrentTick()));
        ASSERT_THAT(IsTrue(Source->GetCurrentTick() > Tick));
        ASSERT_THAT(AreEqual(Source->ComputeStateHash(), Restored->ComputeStateHash()));
        FGuid SourceRoot, RestoredRoot;
        FString RootError;
        ASSERT_THAT(IsTrue(Source->ComputeCanonicalStateRoot(SourceRoot, RootError)));
        ASSERT_THAT(IsTrue(Restored->ComputeCanonicalStateRoot(RestoredRoot, RootError)));
        ASSERT_THAT(IsTrue(SourceRoot == RestoredRoot));
        UE_LOG(LogTemp, Display, TEXT("EditorSessionRoot tick=%d root=%s"),
            Source->GetCurrentTick(), *SourceRoot.ToString(EGuidFormats::Digits));
    }
    ASSERT_THAT(IsTrue(Package->IsDirty()));

    // A strict profile remains restrictive even inside an ordinary editor process.
    FSeinSimulationContentManifestProfile StrictProfile;
    StrictProfile.BuilderRevision = FSeinSimulationContentManifestCodec::CurrentBuilderRevision;
    StrictProfile.Contributors.Add({TEXT("sein.test"), 1, FGuid(1, 2, 3, 4)});
    FString Error;
    ASSERT_THAT(IsTrue(FSeinSimulationContentManifestCodec::SealProfile(
        FSeinSimulationContentManifestCodec::CurrentFormatVersion, StrictProfile, Error)));
    auto Catalog = FSeinPoolObjectCodecRegistry::CaptureManifest(StrictProfile, &Error);
    FSeinSnapshotPoolInstanceRecord Record;
    ASSERT_THAT(IsFalse(FSeinPoolObjectCodecRegistry::CaptureObject(Catalog,
        *Source->GetAbilityInstance(AbilityId), ESeinPoolObjectKind::Ability, AbilityId, Record, Error)));
}

TEST(VisionAuthoringStartsWithOneStampAndPreservesExplicitEmpty,
    "SeinARTS.Editor.SimulationContent")
{
    USeinVisionComponent* Component = NewObject<USeinVisionComponent>();
    ASSERT_THAT(AreEqual(1, Component->Vision.VisionStamps.Num()));
    FInstancedStruct Payload;
    ASSERT_THAT(IsTrue(Component->WritePayload(Payload)));
    ASSERT_THAT(AreEqual(1, Payload.Get<FSeinVisionPayload>().VisionStamps.Num()));
    Component->Vision.VisionStamps.Reset();
    USeinVisionComponent* Duplicate = DuplicateObject(Component, GetTransientPackage());
    ASSERT_THAT(AreEqual(0, Duplicate->Vision.VisionStamps.Num()));
    ASSERT_THAT(IsTrue(Duplicate->WritePayload(Payload)));
    ASSERT_THAT(AreEqual(0, Payload.Get<FSeinVisionPayload>().VisionStamps.Num()));
    ASSERT_THAT(AreEqual(0, FSeinVisionPayload().VisionStamps.Num()));
}
