/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfile.cpp
 */

#include "Balance/SeinBalanceProfile.h"

#if WITH_EDITOR
#include "Actor/SeinActor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	/** Resolve a class path name — native (`/Script/Module.AClass`) or a
	 *  Blueprint-generated class (`/Game/Path/BP.BP_C`) — to its UClass,
	 *  loading the Blueprint if it isn't in memory yet. Null on failure. */
	UClass* ResolveClassFromPath(const FTopLevelAssetPath& Path)
	{
		const FString PathStr = Path.ToString();
		if (UClass* Found = FindObject<UClass>(nullptr, *PathStr))
		{
			return Found;
		}
		return LoadObject<UClass>(nullptr, *PathStr);
	}
}

void USeinBalanceProfile::ResolveTargetClasses(TArray<UClass*>& OutClasses) const
{
	OutClasses.Reset();

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	// Excluded = each excluded class + its whole subtree. Built first so the
	// included traversal can prune excluded branches (and we post-filter too).
	TSet<FTopLevelAssetPath> Excluded;
	for (const TSoftClassPtr<ASeinActor>& Ex : ExcludedClasses)
	{
		UClass* ExClass = Ex.LoadSynchronous();
		if (!ExClass) continue;
		const FTopLevelAssetPath ExPath = ExClass->GetClassPathName();
		Excluded.Add(ExPath);
		AR.GetDerivedClassNames({ ExPath }, TSet<FTopLevelAssetPath>(), Excluded);
	}

	// Included = each root + its subtree, with excluded branches pruned.
	TSet<FTopLevelAssetPath> Included;
	for (const TSoftClassPtr<ASeinActor>& Root : IncludedRoots)
	{
		UClass* RootClass = Root.LoadSynchronous();
		if (!RootClass) continue;
		const FTopLevelAssetPath RootPath = RootClass->GetClassPathName();
		if (!Excluded.Contains(RootPath))
		{
			Included.Add(RootPath);
		}
		AR.GetDerivedClassNames({ RootPath }, Excluded, Included);
	}

	// Resolve to classes, filter, dedupe. Loading every matched Blueprint is
	// acceptable for an explicit editor action; if this gets slow on huge
	// projects, read CLASS_Abstract from the asset-registry tags instead.
	TSet<UClass*> Seen;
	for (const FTopLevelAssetPath& Name : Included)
	{
		if (Excluded.Contains(Name)) continue;

		const FString AssetName = Name.GetAssetName().ToString();
		if (AssetName.StartsWith(TEXT("SKEL_")) || AssetName.StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		UClass* Cls = ResolveClassFromPath(Name);
		if (!Cls || !Cls->IsChildOf(ASeinActor::StaticClass())) continue;
		if (!bIncludeAbstract && Cls->HasAnyClassFlags(CLASS_Abstract)) continue;
		if (Cls->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Deprecated)) continue;

		bool bAlready = false;
		Seen.Add(Cls, &bAlready);
		if (!bAlready)
		{
			OutClasses.Add(Cls);
		}
	}

	OutClasses.Sort([](const UClass& A, const UClass& B)
	{
		return A.GetName() < B.GetName();
	});
}
#endif // WITH_EDITOR
