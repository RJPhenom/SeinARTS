/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinComponentDeletion.cpp
 * @author       RJ Macklem
 * @created      9 Sep 2026
 * @latest       9 Sep 2026
 * @brief        Includes generated payloads in their component Blueprint's deletion operation.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Util/SeinComponentDeletion.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Authoring/SeinEntityComponent.h"
#include "Authoring/SeinPayloadStruct.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "SeinARTSGraphNodesModule.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace SeinComponentDeletion
{
namespace
{
    const FName SourceBlueprintKey(TEXT("SeinSourceBlueprint"));
    FDelegateHandle ExtraObjectsHandle;
    FDelegateHandle PreDeleteHandle;
    FDelegateHandle DeletedHandle;
    FDelegateHandle RenamedHandle;

    struct FHiddenDeletion
    {
        TWeakObjectPtr<UUserDefinedStruct> Struct;
        FAssetData Asset;
    };
    TArray<FHiddenDeletion> PendingHiddenDeletions;

    bool OwnerPathMatches(const FString& Path, const UBlueprint* Blueprint)
    {
        if (Path == Blueprint->GetPathName()) return true;
        // A move can leave the owner stamp at the old path until compilation.
        // Its loaded redirector establishes ownership without changing or
        // regenerating either asset during deletion discovery.
        UObject* Owner = FindObject<UObject>(nullptr, *Path);
        TSet<UObject*> Visited;
        while (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Owner))
        {
            if (Visited.Contains(Owner)) return false;
            Visited.Add(Owner);
            Owner = Redirector->DestinationObject;
        }
        return Owner == Blueprint;
    }

    void OnOwnerRenamed(const FAssetData& Asset, const FString& OldPath)
    {
        const auto* Blueprint = Cast<UBlueprint>(Asset.FastGetAsset(false));
        const UClass* Class = Blueprint ? Blueprint->GeneratedClass : nullptr;
        if (!Class || !Class->IsChildOf(USeinEntityComponent::StaticClass())) return;

        for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
        {
            UUserDefinedStruct* Payload = *It;
            if (!Payload->HasAnyFlags(RF_Public | RF_Standalone)
                || Payload->GetOutermost() == GetTransientPackage()) continue;
            const FString SourcePath = Payload->GetMetaData(SourceBlueprintKey);
            bool bOwned = SourcePath == OldPath;
            if (SourcePath.IsEmpty() && Payload->IsA<USeinPayloadStruct>())
            {
                FString Name = Payload->GetName();
                bOwned = Name.RemoveFromEnd(TEXT("Data"))
                    && Payload->GetOutermost()->GetName() + TEXT(".") + Name == OldPath;
            }
            if (!bOwned) continue;

            // Asset Tools can repair every reference and omit the redirector.
            // Preserve exact ownership at the rename event instead of relying
            // on the old path continuing to resolve after the move.
            Payload->Modify();
            Payload->SetMetaData(SourceBlueprintKey, *Blueprint->GetPathName());
            if (auto* EditorData = Cast<UUserDefinedStructEditorData>(Payload->EditorData))
            {
                EditorData->Modify();
                EditorData->MetaData.FindOrAdd(SourceBlueprintKey) = Blueprint->GetPathName();
            }
            Payload->MarkPackageDirty();
        }
    }

    void AddOwnedPayloads(const TArray<UObject*>& Objects, TSet<UObject*>& ExtraObjects)
    {
        for (UObject* Object : Objects)
        {
            UBlueprint* Blueprint = Cast<UBlueprint>(Object);
            UClass* Class = Blueprint ? Blueprint->GeneratedClass : nullptr;
            if (!Class || !Class->IsChildOf(USeinEntityComponent::StaticClass())) continue;
            const auto* ParentCDO = Class->GetSuperClass()
                ? Cast<USeinEntityComponent>(Class->GetSuperClass()->GetDefaultObject(false)) : nullptr;
            const UUserDefinedStruct* Inherited = ParentCDO ? ParentCDO->PayloadStruct.Get() : nullptr;

            for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
            {
                UUserDefinedStruct* Payload = *It;
                if (Payload == Inherited || !Payload->HasAnyFlags(RF_Public | RF_Standalone)
                    || Payload->GetOutermost() == GetTransientPackage()) continue;

                bool bOwned = false;
                if (Payload->HasMetaData(SourceBlueprintKey))
                {
                    bOwned = OwnerPathMatches(Payload->GetMetaData(SourceBlueprintKey), Blueprint);
                }
                else if (Payload->IsA<USeinPayloadStruct>())
                {
                    // Older embedded payloads predate the owner stamp. Their
                    // companion Blueprint (or move redirector) must resolve
                    // to this exact owner; a matching basename alone is unsafe.
                    FString Name = Payload->GetName();
                    if (Name.RemoveFromEnd(TEXT("Data")))
                    {
                        bOwned = OwnerPathMatches(Payload->GetOutermost()->GetName() + TEXT(".") + Name, Blueprint);
                    }
                }
                if (bOwned) ExtraObjects.Add(Payload);
            }
        }
    }

    void BeforeDelete(const TArray<UObject*>& Objects)
    {
        // A previous delete dialog may have been canceled. Never carry its
        // weak objects into an unrelated deletion after they unload or reload.
        PendingHiddenDeletions.Reset();
        for (UObject* Object : Objects)
        {
            auto* Payload = Cast<USeinPayloadStruct>(Object);
            if (!Payload || !Payload->HasAnyFlags(RF_Public | RF_Standalone)) continue;
            if (!PendingHiddenDeletions.ContainsByPredicate([Payload](const FHiddenDeletion& Entry)
                { return Entry.Struct.Get() == Payload; }))
            {
                PendingHiddenDeletions.Add({Payload, FAssetData(Payload)});
            }
        }
    }

    void AfterDelete(const TArray<UClass*>& Classes)
    {
        auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        for (int32 Index = PendingHiddenDeletions.Num() - 1; Index >= 0; --Index)
        {
            const FHiddenDeletion& Entry = PendingHiddenDeletions[Index];
            const UUserDefinedStruct* Payload = Entry.Struct.Get();
            if (Payload && Payload->HasAnyFlags(RF_Public | RF_Standalone)) continue;

            // AssetDeleted intentionally ignores IsAsset()==false objects.
            // Struct Viewer still indexes these loaded types by object path,
            // so notify it only once the engine actually deleted the payload.
            // This publishes removal; it never registers the hidden payload.
            Registry.OnAssetRemoved().Broadcast(Entry.Asset);
            PendingHiddenDeletions.RemoveAt(Index);
        }
        PendingHiddenDeletions.Reset();
        if (auto* GraphNodes = FModuleManager::GetModulePtr<FSeinARTSGraphNodesModule>(TEXT("SeinARTSGraphNodes")))
        {
            GraphNodes->RequestComponentActionsRefresh();
        }
    }
}

void Register()
{
    ExtraObjectsHandle = FEditorDelegates::OnAddExtraObjectsToDelete.AddStatic(&AddOwnedPayloads);
    PreDeleteHandle = FEditorDelegates::OnAssetsPreDelete.AddStatic(&BeforeDelete);
    DeletedHandle = FEditorDelegates::OnAssetsDeleted.AddStatic(&AfterDelete);
    RenamedHandle = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
        .Get().OnAssetRenamed().AddStatic(&OnOwnerRenamed);
}

void Unregister()
{
    FEditorDelegates::OnAddExtraObjectsToDelete.Remove(ExtraObjectsHandle);
    FEditorDelegates::OnAssetsPreDelete.Remove(PreDeleteHandle);
    FEditorDelegates::OnAssetsDeleted.Remove(DeletedHandle);
    if (auto* Registry = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
    {
        Registry->Get().OnAssetRenamed().Remove(RenamedHandle);
    }
    PendingHiddenDeletions.Reset();
}
}
