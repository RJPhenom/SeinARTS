/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinARTSGraphNodesModule.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Owns typed component action discovery, refresh, and module cleanup.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */

#include "SeinARTSGraphNodesModule.h"

#include "BlueprintActionDatabase.h"
#include "Editor.h"
#include "Misc/CoreDelegates.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Authoring/SeinEntityComponentBlueprint.h"
#include "Containers/Ticker.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "BlueprintNodeSpawner.h"
#include "Framework/Application/SlateApplication.h"
#include "Graph/K2Node_SeinGetComponent.h"
#include "Graph/K2Node_SeinAbilityInputs.h"
#include "UObject/UObjectIterator.h"
#include "Graph/K2Node_SeinSetComponent.h"
#include "Graph/SeinComponentNodeMenuCache.h"
#include "Widgets/SeinWidgetBlueprint.h"
#include "WidgetBlueprint.h"
#include "KismetCompiler.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	void ReleaseNodeClassActions(
		FBlueprintActionDatabase& ActionDatabase,
		UClass* NodeClass)
	{
		const FBlueprintActionDatabase::FActionRegistry& Registry =
			ActionDatabase.GetAllActions();
		const FBlueprintActionDatabase::FActionList* Actions =
			Registry.Find(FObjectKey(NodeClass));

		if (Actions != nullptr)
		{
			for (UBlueprintNodeSpawner* Spawner : *Actions)
			{
				if (Spawner == nullptr)
				{
					continue;
				}

				// Clear executable module callbacks before dropping the action
				// database's GC reference to the spawner. A menu may have
				// primed and cached a live template node already.
				Spawner->CustomizeNodeDelegate.Unbind();
				Spawner->ClearCachedTemplateNode();
			}
		}

		ActionDatabase.ClearAssetActions(NodeClass);
	}
}

void FSeinARTSGraphNodesModule::StartupModule()
{
	bModuleOwnedStateReleased = false;
	RegisterAbilityCompileHook();
	AbilityEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FSeinARTSGraphNodesModule::RegisterAbilityCompileHook);

	// UMG's compiler registry is keyed by the exact Blueprint asset class and
	// does not walk its inheritance chain. Register from this UncookedOnly
	// module (rather than the Editor-typed tooling module) so editor, cook
	// commandlets, and UnrealEditor -game all compile/load Sein widget assets
	// with their WidgetTree intact.
	FKismetCompilerContext::RegisterCompilerForBP(
		USeinWidgetBlueprint::StaticClass(),
		&UWidgetBlueprint::GetCompilerForWidgetBP);

	if (GIsEditor && !IsRunningCommandlet())
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")).Get();
		ComponentAssetAdded = Registry.OnAssetAdded().AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentAssetChanged);
		ComponentAssetRemoved = Registry.OnAssetRemoved().AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentAssetChanged);
		ComponentAssetUpdated = Registry.OnAssetUpdated().AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentAssetChanged);
		ComponentAssetRenamed = Registry.OnAssetRenamed().AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentAssetRenamed);
		ComponentFilesLoaded = Registry.OnFilesLoaded().AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentFilesLoaded);
		ComponentObjectLoaded = FCoreUObjectDelegates::OnAssetLoaded.AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentObjectLoaded);
		ComponentPreDelete = FEditorDelegates::OnAssetsPreDelete.AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentsPreDelete);
		ComponentPreForceDelete = FEditorDelegates::OnPreForceDeleteObjects.AddRaw(this, &FSeinARTSGraphNodesModule::OnComponentsPreDelete);
		RequestComponentActionsRefresh(true);
	}
}

void FSeinARTSGraphNodesModule::RequestComponentActionsRefresh(bool bDiscoverAssets)
{
	if (bModuleOwnedStateReleased || bRefreshingComponentActions || !GIsEditor || IsRunningCommandlet())
	{
		return;
	}
	bDiscoverComponentAssets |= bDiscoverAssets;
	if (!ComponentActionsTicker.IsValid())
	{
		ComponentActionsTicker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FSeinARTSGraphNodesModule::TickComponentActions));
	}
}

void FSeinARTSGraphNodesModule::OnComponentsPreDelete(const TArray<UObject*>& Objects)
{
	if (!Objects.ContainsByPredicate([](const UObject* Object)
		{ return Object && (Object->IsA<USeinEntityComponentBlueprint>() || Object->IsA<UUserDefinedStruct>()); })) return;
	if (FBlueprintActionDatabase* Database = FBlueprintActionDatabase::TryGet())
	{
		ReleaseNodeClassActions(*Database, UK2Node_SeinGetComponent::StaticClass());
		ReleaseNodeClassActions(*Database, UK2Node_SeinSetComponent::StaticClass());
	}
	SeinComponentNodeMenu::ResetCandidateCache();
	// Also restores actions if the designer cancels the delete dialog.
	RequestComponentActionsRefresh();
}

void FSeinARTSGraphNodesModule::OnComponentAssetChanged(const FAssetData& Asset)
{
	if (Asset.AssetClassPath == USeinEntityComponentBlueprint::StaticClass()->GetClassPathName()
		|| Asset.AssetClassPath == UUserDefinedStruct::StaticClass()->GetClassPathName())
	{
		RequestComponentActionsRefresh(true);
	}
}

void FSeinARTSGraphNodesModule::OnComponentAssetRenamed(const FAssetData& Asset, const FString& OldPath)
{
	OnComponentAssetChanged(Asset);
}

void FSeinARTSGraphNodesModule::OnComponentObjectLoaded(UObject* Object)
{
	if (Object && (Object->IsA<USeinEntityComponentBlueprint>() || Object->IsA<UUserDefinedStruct>()))
	{
		RequestComponentActionsRefresh();
	}
}

void FSeinARTSGraphNodesModule::OnComponentFilesLoaded()
{
	RequestComponentActionsRefresh(true);
}

bool FSeinARTSGraphNodesModule::TickComponentActions(float DeltaTime)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	if (Registry.IsLoadingAssets() || IsAsyncLoading() || UE::IsSavingPackage() || GCompilingBlueprint || IsGarbageCollecting())
	{
		return true;
	}
	ComponentActionsTicker.Reset();
	TGuardValue<bool> RefreshGuard(bRefreshingComponentActions, true);
	if (bDiscoverComponentAssets)
	{
		bDiscoverComponentAssets = false;
		// Generated payloads deliberately report IsAsset() == false. Load their
		// owning component Blueprints as well as legacy standalone UDS assets.
		// Do this outside GetMenuActions: loading can re-enter the action database.
		FARFilter Filter;
		Filter.ClassPaths.Add(USeinEntityComponentBlueprint::StaticClass()->GetClassPathName());
		Filter.ClassPaths.Add(UUserDefinedStruct::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		TArray<FAssetData> Assets;
		Registry.GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			// A deferred registry removal can briefly retain an unloaded asset
			// after its package was deleted or its content mount was removed.
			const FString PackageName = Asset.PackageName.ToString();
			if (Asset.IsAssetLoaded() || (FPackageName::IsValidLongPackageName(PackageName, true)
				&& FPackageName::DoesPackageExist(PackageName)))
			{
				Asset.GetAsset();
			}
		}
	}

	// The node actions are keyed by node class, so an ordinary Blueprint or
	// struct asset refresh cannot rebuild them. Invalidate even within the
	// same frame: a newly generated payload may follow an earlier menu scan.
	SeinComponentNodeMenu::ResetCandidateCache();
	FBlueprintActionDatabase& Database = FBlueprintActionDatabase::Get();
	Database.RefreshClassActions(UK2Node_SeinGetComponent::StaticClass());
	Database.RefreshClassActions(UK2Node_SeinSetComponent::StaticClass());
	return false;
}

void FSeinARTSGraphNodesModule::PreUnloadCallback()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSGraphNodesModule::ShutdownModule()
{
	ReleaseModuleOwnedState();
}

void FSeinARTSGraphNodesModule::ReleaseModuleOwnedState()
{
	if (bModuleOwnedStateReleased)
	{
		return;
	}
	bModuleOwnedStateReleased = true;
	FCoreDelegates::GetOnPostEngineInit().Remove(AbilityEngineInitHandle);
	if (GEditor) GEditor->OnBlueprintCompiled().Remove(AbilityCompiledHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(AbilityInputsTicker);
	FTSTicker::GetCoreTicker().RemoveTicker(ComponentActionsTicker);
	ComponentActionsTicker.Reset();
	FCoreUObjectDelegates::OnAssetLoaded.Remove(ComponentObjectLoaded);
	FEditorDelegates::OnAssetsPreDelete.Remove(ComponentPreDelete);
	FEditorDelegates::OnPreForceDeleteObjects.Remove(ComponentPreForceDelete);
	if (FAssetRegistryModule* Module = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
	{
		IAssetRegistry& Registry = Module->Get();
		Registry.OnAssetAdded().Remove(ComponentAssetAdded);
		Registry.OnAssetRemoved().Remove(ComponentAssetRemoved);
		Registry.OnAssetUpdated().Remove(ComponentAssetUpdated);
		Registry.OnAssetRenamed().Remove(ComponentAssetRenamed);
		Registry.OnFilesLoaded().Remove(ComponentFilesLoaded);
	}
	bDiscoverComponentAssets = false;

	// Open Blueprint action menus may retain their own references to the
	// spawners. Close them while this generation's callbacks are still valid.
	if (!IsEngineExitRequested() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().DismissAllMenus();
	}

	if (UObjectInitialized() && !IsEngineExitRequested())
	{
		if (FBlueprintActionDatabase* ActionDatabase =
				FBlueprintActionDatabase::TryGet())
		{
			ReleaseNodeClassActions(
				*ActionDatabase,
				UK2Node_SeinGetComponent::StaticClass());
			ReleaseNodeClassActions(
				*ActionDatabase,
				UK2Node_SeinSetComponent::StaticClass());
			ReleaseNodeClassActions(*ActionDatabase, UK2Node_SeinAbilityInputs::StaticClass());
		}
	}

	SeinComponentNodeMenu::ResetCandidateCache();
}

IMPLEMENT_MODULE(FSeinARTSGraphNodesModule, SeinARTSGraphNodes)

void FSeinARTSGraphNodesModule::RegisterAbilityCompileHook()
{
	if (GEditor && !AbilityCompiledHandle.IsValid()) AbilityCompiledHandle = GEditor->OnBlueprintCompiled().AddRaw(this, &FSeinARTSGraphNodesModule::OnAbilityBlueprintCompiled);
}

void FSeinARTSGraphNodesModule::OnAbilityBlueprintCompiled()
{
	if (!AbilityInputsTicker.IsValid()) AbilityInputsTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FSeinARTSGraphNodesModule::TickAbilityInputs));
}
bool FSeinARTSGraphNodesModule::TickAbilityInputs(float)
{
	if (GCompilingBlueprint || IsAsyncLoading() || IsGarbageCollecting() || UE::IsSavingPackage()) return true;
	AbilityInputsTicker.Reset();
	for (TObjectIterator<UK2Node_SeinAbilityInputs> It; It; ++It)
		if (!It->IsTemplate() && It->GetGraph()) It->RefreshInputPins();
	return false;
}
