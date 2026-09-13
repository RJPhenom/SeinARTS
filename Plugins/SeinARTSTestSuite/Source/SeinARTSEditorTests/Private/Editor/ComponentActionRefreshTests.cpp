/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         ComponentActionRefreshTests.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Exercises component action discovery across compile and package unload.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Misc/AutomationTest.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Authoring/SeinEntityComponent.h"
#include "Authoring/SeinEntityComponentBlueprint.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Editor.h"
#include "K2Node_CallFunction.h"
#include "Lib/SeinSimMutationBPFL.h"
#include "ObjectTools.h"
#include "StructViewerModule.h"
#include "StructViewerFilter.h"
#include "Widgets/SWidget.h"
#include "Graph/K2Node_SeinGetComponent.h"
#include "Graph/K2Node_SeinSetComponent.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Factories/SeinSimComponentFactory.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "SeinARTSGraphNodesModule.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/ObjectKey.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Util/SeinDataComponentSync.h"

namespace SeinComponentActionTests
{
    void ClearTypedActions()
    {
        auto& Database = FBlueprintActionDatabase::Get();
        Database.ClearAssetActions(UK2Node_SeinGetComponent::StaticClass());
        Database.ClearAssetActions(UK2Node_SeinSetComponent::StaticClass());
    }

    template <typename TNode>
    int32 CountActions(const FString& StructPath)
    {
        const auto* Actions = FBlueprintActionDatabase::Get().GetAllActions().Find(FObjectKey(TNode::StaticClass()));
        int32 Count = 0;
        if (Actions)
        {
            for (const UBlueprintNodeSpawner* Spawner : *Actions)
            {
                const auto* Node = Cast<TNode>(Spawner->GetTemplateNode());
                if (Node && Node->SelectedStruct && Node->SelectedStruct->GetPathName() == StructPath)
                {
                    ++Count;
                }
            }
        }
        return Count;
    }

    struct FFixture
    {
        FString Root;
        FString Directory;
        FString PackageName;
        FString BlueprintPath;
        FString StructPath;
        TArray<FString> OtherPackages;
        bool bTestUnloaded = false;
        bool bCancelBeforeUnload = false;
        int32 Stage = 0;
        double Started = FPlatformTime::Seconds();

        explicit FFixture(bool bUnloaded) : bTestUnloaded(bUnloaded)
        {
            const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
            Root = TEXT("/SeinComponentActions_") + Id + TEXT("/");
            Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/ComponentActions") / Id) + TEXT("/");
            IFileManager::Get().MakeDirectory(*Directory, true);
            FPackageName::RegisterMountPoint(Root, Directory);
            PackageName = Root + TEXT("SC_MenuFixture");
            BlueprintPath = PackageName + TEXT(".SC_MenuFixture");
            StructPath = PackageName + TEXT(".SC_MenuFixtureData");

            // Prime the action database BEFORE this type exists, as in an
            // already-open ability graph when a designer creates a component.
            auto& Database = FBlueprintActionDatabase::Get();
            Database.RefreshClassActions(UK2Node_SeinGetComponent::StaticClass());
            Database.RefreshClassActions(UK2Node_SeinSetComponent::StaticClass());
            UPackage* Package = CreatePackage(*PackageName);
            UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
                USeinEntityComponent::StaticClass(), Package, TEXT("SC_MenuFixture"),
                BPTYPE_Normal, USeinEntityComponentBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
            FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Stock"),
                FEdGraphPinType(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType()));
            FKismetEditorUtilities::CompileBlueprint(Blueprint);
        }

        ~FFixture()
        {
            ClearTypedActions();
            if (UBlueprint* Blueprint = FindObject<UBlueprint>(nullptr, *BlueprintPath))
            {
                FAssetRegistryModule::AssetDeleted(Blueprint);
            }
            TArray<UPackage*> Packages;
            OtherPackages.AddUnique(PackageName);
            for (const FString& Name : OtherPackages)
            {
                if (UPackage* Package = FindPackage(nullptr, *Name)) Packages.AddUnique(Package);
            }
            FText Error;
            UPackageTools::UnloadPackages(Packages, Error, true);
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().WaitForCompletion();
            FPackageName::UnRegisterMountPoint(Root, Directory);
            IFileManager::Get().DeleteDirectory(*Directory, false, true);
            FModuleManager::GetModuleChecked<FSeinARTSGraphNodesModule>(TEXT("SeinARTSGraphNodes")).RequestComponentActionsRefresh();
        }
    };

    class FVerifyActions : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        TSharedRef<FFixture> Fixture;
    public:
        FVerifyActions(FAutomationTestBase* InTest, TSharedRef<FFixture> InFixture)
            : Test(InTest), Fixture(InFixture) {}

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() - Fixture->Started > 30.0)
            {
                Test->AddError(TEXT("Component Get/Set actions did not refresh after compilation/discovery."));
                return true;
            }
            if (CountActions<UK2Node_SeinGetComponent>(Fixture->StructPath) != 1
                || CountActions<UK2Node_SeinSetComponent>(Fixture->StructPath) != 1)
            {
                return false;
            }
            if (Fixture->Stage == 0)
            {
                auto* Blueprint = FindObject<UBlueprint>(nullptr, *Fixture->BlueprintPath);
                if (!Test->TestNotNull(TEXT("Compiled component Blueprint"), Blueprint)) return true;
                auto* ExistingPayload = FindObject<UUserDefinedStruct>(nullptr, *Fixture->StructPath);
                auto* EditorData = ExistingPayload ? Cast<UUserDefinedStructEditorData>(ExistingPayload->EditorData) : nullptr;
                if (!Test->TestNotNull(TEXT("Generated payload editor data"), EditorData)) return true;
                // Simulate a legacy payload whose live owner stamp was never
                // persisted into the metadata restored by UDS compilation.
                EditorData->MetaData.Remove(TEXT("SeinSourceBlueprint"));
                ExistingPayload->GetOutermost()->SetDirtyFlag(false);
                SeinDataComponentSync::SyncPayloadStructForBlueprint(Blueprint);
                Test->TestTrue(TEXT("Metadata-only ownership repair dirties the payload package"), ExistingPayload->GetOutermost()->IsDirty());
                Test->TestEqual(TEXT("Owner is persisted in compiler metadata"),
                    EditorData->MetaData.FindRef(TEXT("SeinSourceBlueprint")), Fixture->BlueprintPath);
                if (!Fixture->bTestUnloaded)
                {
                    // Recompile without manual database calls and require the
                    // same single pair of correctly typed actions afterward.
                    FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("Capacity"),
                        FEdGraphPinType(UEdGraphSchema_K2::PC_Int, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType()));
                    FKismetEditorUtilities::CompileBlueprint(Blueprint);
                    Fixture->Stage = 1;
                    return false;
                }
                const FString Filename = FPackageName::LongPackageNameToFilename(Fixture->PackageName, FPackageName::GetAssetPackageExtension());
                FSavePackageArgs Args;
                Args.TopLevelFlags = RF_Public | RF_Standalone;
                if (!Test->TestTrue(TEXT("Save component fixture"), UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, Args))) return true;
                auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
                Registry.ScanFilesSynchronous({Filename}, true);
                if (Fixture->bCancelBeforeUnload)
                {
                    TArray<UObject*> Deletion{Blueprint};
                    ObjectTools::AddExtraObjectsToDelete(Deletion);
                    FEditorDelegates::OnAssetsPreDelete.Broadcast(Deletion);
                }
                ClearTypedActions();
                TWeakObjectPtr<UBlueprint> WeakBlueprint(Blueprint);
                TWeakObjectPtr<UUserDefinedStruct> WeakPayload(FindObject<UUserDefinedStruct>(nullptr, *Fixture->StructPath));
                FText Error;
                const bool bUnloaded = UPackageTools::UnloadPackages({Blueprint->GetOutermost()}, Error, true);
                if (!Test->TestTrue(TEXT("Unload saved fixture: ") + Error.ToString(), bUnloaded)) return true;
                if (!Test->TestFalse(TEXT("Blueprint is actually unloaded"), WeakBlueprint.IsValid())) return true;
                if (!Test->TestFalse(TEXT("Hidden payload is actually unloaded"), WeakPayload.IsValid())) return true;
                // The same discovery request used at startup must find the
                // registry asset and its hidden, non-asset payload from disk.
                FModuleManager::GetModuleChecked<FSeinARTSGraphNodesModule>(TEXT("SeinARTSGraphNodes")).RequestComponentActionsRefresh(true);
                Fixture->Stage = 1;
                return false;
            }

            auto* Payload = FindObject<UUserDefinedStruct>(nullptr, *Fixture->StructPath);
            if (!Test->TestNotNull(TEXT("Menu resolves the payload"), Payload)) return true;
            auto& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
            Test->TestFalse(TEXT("Picker notification does not register the hidden payload as an asset"),
                AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(Fixture->StructPath), true).IsValid());
            Test->TestEqual(TEXT("Ownership metadata survives compile and reload"),
                Payload->GetMetaData(TEXT("SeinSourceBlueprint")), Fixture->BlueprintPath);
            if (Fixture->bCancelBeforeUnload)
            {
                // An unrelated real deletion after cancel/unload/reload must
                // not remove the surviving payload from the picker hierarchy.
                int32 FalseRemovals = 0;
                auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
                const FDelegateHandle Handle = Registry.OnAssetRemoved().AddLambda([&](const FAssetData& Asset)
                {
                    if (Asset.GetSoftObjectPath().ToString() == Fixture->StructPath) ++FalseRemovals;
                });
                const FString Name = Fixture->Root + TEXT("BP_Unrelated");
                Fixture->OtherPackages.Add(Name);
                auto* Unrelated = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(),
                    CreatePackage(*Name), TEXT("BP_Unrelated"), BPTYPE_Normal,
                    UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
                Test->TestEqual(TEXT("Unrelated deletion succeeds"), ObjectTools::ForceDeleteObjects({Unrelated}, false), 1);
                Registry.OnAssetRemoved().Remove(Handle);
                Test->TestEqual(TEXT("Canceled deletion never reports the reloaded payload deleted"), FalseRemovals, 0);
                Test->TestTrue(TEXT("Reloaded payload remains public"), Payload->HasAnyFlags(RF_Public | RF_Standalone));
            }
            if (!Fixture->bTestUnloaded)
            {
                // UDS field names carry stable GUID suffixes.
                bool bFoundCapacity = false;
                for (TFieldIterator<FProperty> It(Payload); It; ++It)
                {
                    bFoundCapacity |= It->GetName().StartsWith(TEXT("Capacity_"));
                }
                if (!bFoundCapacity) return false;
                Test->TestTrue(TEXT("Edited field reaches the menu's payload type"), bFoundCapacity);
            }
            Test->TestEqual(TEXT("Exactly one getter"), CountActions<UK2Node_SeinGetComponent>(Fixture->StructPath), 1);
            Test->TestEqual(TEXT("Exactly one setter"), CountActions<UK2Node_SeinSetComponent>(Fixture->StructPath), 1);
            return true;
        }
    };

    class FObservedStructFilter : public IStructViewerFilter
    {
    public:
        FString Target;
        bool bSawTarget = false;
        int32 Visited = 0;
        virtual bool IsStructAllowed(const FStructViewerInitializationOptions&, const UScriptStruct* Struct,
            TSharedRef<FStructViewerFilterFuncs>) override
        {
            ++Visited;
            bSawTarget |= Struct->GetPathName() == Target;
            return true;
        }
        virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions&, const FSoftObjectPath& Path,
            TSharedRef<FStructViewerFilterFuncs>) override
        {
            ++Visited;
            bSawTarget |= Path.ToString() == Target;
            return true;
        }
    };

    class FVerifyDeletion : public IAutomationLatentCommand
    {
        FAutomationTestBase* Test;
        TSharedRef<FFixture> Fixture;
        bool bMove;
        int32 Stage = 0;
        TWeakObjectPtr<UUserDefinedStruct> DeletedPayload;
        TWeakObjectPtr<UK2Node_CallFunction> DeltaNode;
        TSharedPtr<FObservedStructFilter> Filter;
        TSharedPtr<SWidget> Picker;
    public:
        FVerifyDeletion(FAutomationTestBase* InTest, bool bInMove)
            : Test(InTest), Fixture(MakeShared<FFixture>(false)), bMove(bInMove) {}

        virtual bool Update() override
        {
            if (FPlatformTime::Seconds() - Fixture->Started > 30.0)
            {
                Test->AddError(TEXT("Timed out waiting for component deletion lifecycle."));
                return true;
            }
            if (Stage == 0)
            {
                if (CountActions<UK2Node_SeinGetComponent>(Fixture->StructPath) != 1
                    || CountActions<UK2Node_SeinSetComponent>(Fixture->StructPath) != 1) return false;
                auto* Blueprint = FindObject<UBlueprint>(nullptr, *Fixture->BlueprintPath);
                auto* Payload = FindObject<UUserDefinedStruct>(nullptr, *Fixture->StructPath);
                if (!Test->TestNotNull(TEXT("Generated payload"), Payload)) return true;
                DeletedPayload = Payload;
                FSavePackageArgs Args;
                Args.TopLevelFlags = RF_Public | RF_Standalone;
                const FString Filename = FPackageName::LongPackageNameToFilename(Fixture->PackageName, FPackageName::GetAssetPackageExtension());
                if (!Test->TestTrue(TEXT("Save component before deletion"), UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, Args))) return true;

                if (bMove)
                {
                    const FString OriginalOwnerPath = Blueprint->GetPathName();
                    Payload->SetMetaData(TEXT("SeinSourceBlueprint"), *OriginalOwnerPath);
                    Fixture->OtherPackages.Add(Fixture->PackageName);
                    auto& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
                    if (!Test->TestTrue(TEXT("Move/rename the component through Asset Tools"), AssetTools.RenameAssets(
                        {FAssetRenameData(TWeakObjectPtr<UObject>(Blueprint), Fixture->Root + TEXT("Moved"), FString(TEXT("SC_Renamed")))}))) return true;
                    Fixture->PackageName = Blueprint->GetOutermost()->GetName();
                    Fixture->BlueprintPath = Blueprint->GetPathName();
                    Test->TestNotEqual(TEXT("Payload remains in its original package"), Payload->GetOutermost(), Blueprint->GetOutermost());
                    Test->TestEqual(TEXT("Move updates the payload owner stamp without compiling"),
                        Payload->GetMetaData(TEXT("SeinSourceBlueprint")), Fixture->BlueprintPath);

                    // Persist both packages, then reload before deletion so
                    // the owner stamp must survive an editor restart boundary.
                    TArray<UPackage*> Packages{Blueprint->GetOutermost(), Payload->GetOutermost()};
                    for (UPackage* Package : Packages)
                    {
                        const FString SavedFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
                        if (!Test->TestTrue(TEXT("Save moved owner and payload"), UPackage::SavePackage(Package, nullptr, *SavedFilename, Args))) return true;
                    }
                    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
                    Registry.WaitForCompletion();
                    ClearTypedActions();
                    TWeakObjectPtr<UBlueprint> OldBlueprint(Blueprint);
                    FText Error;
                    if (!Test->TestTrue(TEXT("Unload moved owner and payload: ") + Error.ToString(),
                        UPackageTools::UnloadPackages(Packages, Error, true))) return true;
                    Test->TestFalse(TEXT("Moved Blueprint actually unloaded"), OldBlueprint.IsValid());
                    Test->TestFalse(TEXT("Moved payload actually unloaded"), DeletedPayload.IsValid());
                    Blueprint = LoadObject<UBlueprint>(nullptr, *Fixture->BlueprintPath);
                    Payload = FindObject<UUserDefinedStruct>(nullptr, *Fixture->StructPath);
                    if (!Test->TestNotNull(TEXT("Reload moved owner"), Blueprint)
                        || !Test->TestNotNull(TEXT("Reload old-package payload"), Payload)) return true;
                    DeletedPayload = Payload;
                    Test->TestEqual(TEXT("Moved ownership survives save and reload"),
                        Payload->GetMetaData(TEXT("SeinSourceBlueprint")), Fixture->BlueprintPath);
                }

                // A child CDO can borrow its parent's PayloadStruct before its
                // own first sync. Delete discovery must never claim that type.
                const FString ChildPackageName = Fixture->Root + TEXT("SC_Child");
                Fixture->OtherPackages.Add(ChildPackageName);
                auto* Child = FKismetEditorUtilities::CreateBlueprint(Blueprint->GeneratedClass,
                    CreatePackage(*ChildPackageName), TEXT("SC_Child"), BPTYPE_Normal,
                    USeinEntityComponentBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
                auto* ChildCDO = Child->GeneratedClass->GetDefaultObject<USeinEntityComponent>();
                ChildCDO->PayloadStruct = Payload;
                Test->TestTrue(TEXT("Child fixture borrows the parent's payload"), ChildCDO->PayloadStruct == Payload);
                TArray<UObject*> ChildDeletion{Child};
                ObjectTools::AddExtraObjectsToDelete(ChildDeletion);
                Test->TestFalse(TEXT("Deleting a child preserves its parent's payload"), ChildDeletion.Contains(Payload));
                Test->TestEqual(TEXT("Force-delete the child only"), ObjectTools::ForceDeleteObjects({Child}, false), 1);
                Test->TestTrue(TEXT("Parent payload survives actual child force deletion"), DeletedPayload.IsValid());

                // A manually authored shared struct can have the conventional
                // payload name and be assigned to a CDO. Neither is proof of
                // ownership, so it must not be included in owner deletion.
                const FString SharedPackageName = Fixture->Root + TEXT("Shared");
                Fixture->OtherPackages.Add(SharedPackageName);
                auto* Shared = FStructureEditorUtils::CreateUserDefinedStruct(CreatePackage(*SharedPackageName),
                    FName(*(Blueprint->GetName() + TEXT("Data"))), RF_Public | RF_Standalone);
                USeinSimComponentFactory::MarkUserDefinedStructAsEntityComponent(Shared);
                auto* CDO = Blueprint->GeneratedClass->GetDefaultObject<USeinEntityComponent>();
                CDO->PayloadStruct = Shared;
                TArray<UObject*> ForeignDeletion{Blueprint};
                ObjectTools::AddExtraObjectsToDelete(ForeignDeletion);
                Test->TestFalse(TEXT("Unstamped shared struct is not owned by its referencing component"), ForeignDeletion.Contains(Shared));
                CDO->PayloadStruct = Payload;

                // Exercise the exact Apply Field Delta StructType pin, not a
                // lookalike test property. Keep its Blueprint outside the
                // deleted component package so reference replacement is needed.
                const FString OwnerPackageName = Fixture->Root + TEXT("BP_DeltaOwner");
                Fixture->OtherPackages.Add(OwnerPackageName);
                auto* Owner = FKismetEditorUtilities::CreateBlueprint(UObject::StaticClass(),
                    CreatePackage(*OwnerPackageName), TEXT("BP_DeltaOwner"), BPTYPE_Normal,
                    UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
                UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Owner, TEXT("DeltaGraph"),
                    UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
                FBlueprintEditorUtils::AddUbergraphPage(Owner, Graph);
                FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
                auto* Node = Creator.CreateNode();
                Node->FunctionReference.SetExternalMember(TEXT("SeinApplyFieldDelta"), USeinSimMutationBPFL::StaticClass());
                Creator.Finalize();
                GetDefault<UEdGraphSchema_K2>()->TrySetDefaultObject(*Node->FindPinChecked(TEXT("StructType")), Payload);
                DeltaNode = Node;

                // Construct the same Struct Viewer used by SGraphPinStruct
                // before deletion, so its cached hierarchy must be updated.
                Filter = MakeShared<FObservedStructFilter>();
                Filter->Target = Fixture->StructPath;
                FStructViewerInitializationOptions Options;
                Options.StructFilter = Filter;
                Options.bShowNoneOption = true;
                Picker = FModuleManager::LoadModuleChecked<FStructViewerModule>(TEXT("StructViewer")).CreateStructViewer(Options, FOnStructPicked());
                Picker->Tick(FGeometry(), FPlatformTime::Seconds(), 0.0f);
                if (!Test->TestTrue(TEXT("Picker initially offers the generated payload"), Filter->bSawTarget)) return true;

                TArray<UObject*> Deletion{Blueprint};
                Test->TestEqual(TEXT("Payload ownership survives dependent Blueprint deletion"),
                    Payload->GetMetaData(TEXT("SeinSourceBlueprint")), Fixture->BlueprintPath);
                ObjectTools::AddExtraObjectsToDelete(Deletion);
                Test->TestTrue(TEXT("Owner deletion includes its generated payload"), Deletion.Contains(Payload));
                Test->TestTrue(TEXT("Inspecting deletion does not delete the payload"), Payload->HasAnyFlags(RF_Public | RF_Standalone));
                // Model opening and canceling the delete dialog. References
                // stay intact and the cleared menu actions must return.
                FEditorDelegates::OnAssetsPreDelete.Broadcast(Deletion);
                Stage = 1;
                return false;
            }
            if (Stage == 1)
            {
                if (CountActions<UK2Node_SeinGetComponent>(Fixture->StructPath) != 1
                    || CountActions<UK2Node_SeinSetComponent>(Fixture->StructPath) != 1) return false;
                if (!Test->TestTrue(TEXT("Cancel preserves Apply Field Delta's selected payload"),
                    DeltaNode->FindPinChecked(TEXT("StructType"))->DefaultObject == DeletedPayload.Get())) return true;
                auto* Blueprint = FindObject<UBlueprint>(nullptr, *Fixture->BlueprintPath);
                Test->TestEqual(TEXT("Force deletion succeeds"), ObjectTools::ForceDeleteObjects({Blueprint}, false), 1);
                Stage = 2;
                return false;
            }

            Test->TestFalse(TEXT("Generated payload is actually destroyed"), DeletedPayload.IsValid());
            if (UK2Node_CallFunction* Node = DeltaNode.Get())
            {
                UEdGraphPin* Pin = Node->FindPin(TEXT("StructType"));
                Test->TestTrue(TEXT("Apply Field Delta no longer references the deleted type"),
                    Pin && (!Pin->DefaultObject || Pin->DefaultObject->GetPathName() != Fixture->StructPath));
            }
            else Test->AddError(TEXT("Apply Field Delta node unexpectedly disappeared."));
            Test->TestEqual(TEXT("No deleted getter remains"), CountActions<UK2Node_SeinGetComponent>(Fixture->StructPath), 0);
            Test->TestEqual(TEXT("No deleted setter remains"), CountActions<UK2Node_SeinSetComponent>(Fixture->StructPath), 0);
            Filter->bSawTarget = false;
            Filter->Visited = 0;
            Picker->Tick(FGeometry(), FPlatformTime::Seconds(), 0.0f);
            Test->TestTrue(TEXT("Existing picker refreshes its hierarchy"), Filter->Visited > 0);
            Test->TestFalse(TEXT("Existing picker no longer offers deleted payload"), Filter->bSawTarget);
            return true;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinComponentActionsCompile, "SeinARTS.Editor.ComponentActions.CreateAndCompile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinComponentActionsCompile::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(SeinComponentActionTests::FVerifyActions(this, MakeShared<SeinComponentActionTests::FFixture>(false)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinComponentActionsUnloaded, "SeinARTS.Editor.ComponentActions.DiscoverUnloadedBlueprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinComponentActionsUnloaded::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(SeinComponentActionTests::FVerifyActions(this, MakeShared<SeinComponentActionTests::FFixture>(true)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinComponentActionsDelete, "SeinARTS.Editor.ComponentActions.ForceDeletePayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinComponentActionsDelete::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(SeinComponentActionTests::FVerifyDeletion(this, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinComponentActionsDeleteMoved, "SeinARTS.Editor.ComponentActions.ForceDeleteMovedPayload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinComponentActionsDeleteMoved::RunTest(const FString& Parameters)
{
    ADD_LATENT_AUTOMATION_COMMAND(SeinComponentActionTests::FVerifyDeletion(this, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeinComponentActionsCancelReload, "SeinARTS.Editor.ComponentActions.CancelDeleteReload",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSeinComponentActionsCancelReload::RunTest(const FString& Parameters)
{
    auto Fixture = MakeShared<SeinComponentActionTests::FFixture>(true);
    Fixture->bCancelBeforeUnload = true;
    ADD_LATENT_AUTOMATION_COMMAND(SeinComponentActionTests::FVerifyActions(this, Fixture));
    return true;
}
