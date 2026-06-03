/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFactionService.cpp
 */

#include "Subsystems/SeinFactionService.h"
#include "Data/SeinFaction.h"
#include "Settings/PluginSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/AssetManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFactionService, Log, All);

bool USeinFactionService::ShouldCreateSubsystem(UObject* Outer) const
{
	// Resolve the configured faction-service class. Empty path = use the
	// framework default (this class). Any project override means we only
	// instantiate the override class — the engine creates a USeinFactionService
	// candidate per UCLASS but we want exactly one.
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	if (!Settings) return GetClass() == StaticClass();

	UClass* ConfiguredClass = nullptr;
	if (!Settings->FactionServiceClass.IsNull())
	{
		ConfiguredClass = Settings->FactionServiceClass.TryLoadClass<USeinFactionService>();
	}
	if (!ConfiguredClass)
	{
		ConfiguredClass = USeinFactionService::StaticClass();
	}

	return GetClass() == ConfiguredClass;
}

void USeinFactionService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Initial scan. Cooked builds may not have all assets loaded yet — the
	// FilesLoaded hook below catches the late-arriving ones.
	RefreshFromAssetRegistry();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FilesLoadedHandle = AR.OnFilesLoaded().AddLambda([this]()
	{
		RefreshFromAssetRegistry();
	});

	UE_LOG(LogSeinFactionService, Log, TEXT("USeinFactionService initialized — discovered %d faction asset(s)."),
		CachedAssetFactions.Num());
}

void USeinFactionService::Deinitialize()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		FAssetRegistryModule& ARM = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		ARM.Get().OnFilesLoaded().Remove(FilesLoadedHandle);
	}
	FilesLoadedHandle.Reset();

	CachedAssetFactions.Reset();
	RuntimeFactions.Reset();

	Super::Deinitialize();
}

void USeinFactionService::RefreshFromAssetRegistry()
{
	CachedAssetFactions.Reset();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Assets;
	AR.GetAssetsByClass(USeinFaction::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/ true);

	CachedAssetFactions.Reserve(Assets.Num());
	for (const FAssetData& AssetData : Assets)
	{
		CachedAssetFactions.Add(TSoftObjectPtr<USeinFaction>(AssetData.ToSoftObjectPath()));
	}

	BroadcastFactionsChanged();
}

TArray<TSoftObjectPtr<USeinFaction>> USeinFactionService::GetAvailableFactions() const
{
	TArray<TSoftObjectPtr<USeinFaction>> Out;
	Out.Reserve(CachedAssetFactions.Num() + RuntimeFactions.Num());
	Out.Append(CachedAssetFactions);
	for (const TObjectPtr<USeinFaction>& Faction : RuntimeFactions)
	{
		if (Faction)
		{
			Out.Add(TSoftObjectPtr<USeinFaction>(Faction));
		}
	}
	return Out;
}

bool USeinFactionService::IsFactionValid(FSeinFactionID ID) const
{
	// FactionID 0 is reserved (Neutral); not valid for player slots.
	if (ID.Value == 0) return false;

	// Runtime first (faster, hard refs avoid sync load).
	for (const TObjectPtr<USeinFaction>& Faction : RuntimeFactions)
	{
		if (Faction && Faction->FactionID == ID) return true;
	}

	// Asset-discovered: sync-load to inspect FactionID. Cheap because the
	// asset is already cooked + indexed; the load is only deferred-deserialize
	// of the small data asset, not its referenced actor blueprints.
	for (const TSoftObjectPtr<USeinFaction>& Soft : CachedAssetFactions)
	{
		USeinFaction* Faction = Soft.LoadSynchronous();
		if (Faction && Faction->FactionID == ID) return true;
	}
	return false;
}

TSoftObjectPtr<USeinFaction> USeinFactionService::ResolveFaction(FSeinFactionID ID) const
{
	for (const TObjectPtr<USeinFaction>& Faction : RuntimeFactions)
	{
		if (Faction && Faction->FactionID == ID)
		{
			return TSoftObjectPtr<USeinFaction>(Faction);
		}
	}
	for (const TSoftObjectPtr<USeinFaction>& Soft : CachedAssetFactions)
	{
		USeinFaction* Faction = Soft.LoadSynchronous();
		if (Faction && Faction->FactionID == ID) return Soft;
	}
	return nullptr;
}

TArray<TSoftObjectPtr<USeinFaction>> USeinFactionService::GetAvailableFactionsFiltered(
	const TArray<TSoftObjectPtr<USeinFaction>>& Allowlist) const
{
	const TArray<TSoftObjectPtr<USeinFaction>> All = GetAvailableFactions();
	if (Allowlist.IsEmpty()) return All;

	// Build a set of allowed soft-paths for O(1) lookup.
	TSet<FSoftObjectPath> AllowedPaths;
	AllowedPaths.Reserve(Allowlist.Num());
	for (const TSoftObjectPtr<USeinFaction>& Soft : Allowlist)
	{
		if (!Soft.IsNull()) AllowedPaths.Add(Soft.ToSoftObjectPath());
	}

	TArray<TSoftObjectPtr<USeinFaction>> Out;
	Out.Reserve(All.Num());
	for (const TSoftObjectPtr<USeinFaction>& Soft : All)
	{
		if (AllowedPaths.Contains(Soft.ToSoftObjectPath())) Out.Add(Soft);
	}
	return Out;
}

void USeinFactionService::RegisterRuntimeFaction(USeinFaction* Faction)
{
	if (!Faction) return;
	if (RuntimeFactions.Contains(Faction)) return;
	RuntimeFactions.Add(Faction);

	UE_LOG(LogSeinFactionService, Log, TEXT("Registered runtime faction '%s' (ID=%u)."),
		*Faction->FactionName.ToString(), Faction->FactionID.Value);

	BroadcastFactionsChanged();
}

void USeinFactionService::UnregisterRuntimeFaction(USeinFaction* Faction)
{
	if (!Faction) return;
	const int32 Removed = RuntimeFactions.Remove(Faction);
	if (Removed > 0)
	{
		UE_LOG(LogSeinFactionService, Log, TEXT("Unregistered runtime faction '%s'."),
			*Faction->FactionName.ToString());
		BroadcastFactionsChanged();
	}
}

void USeinFactionService::BroadcastFactionsChanged()
{
	OnFactionsChanged.Broadcast();
	OnFactionsChangedBP.Broadcast();
}
