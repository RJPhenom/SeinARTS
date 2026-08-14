/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfile.cpp
 */

#include "Balance/SeinBalanceProfile.h"

#if WITH_EDITOR
#include "Actor/SeinActor.h"
#include "Abilities/SeinAbility.h"
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

	// Kind-aware base class + roots. Entities read IncludedRoots/ExcludedClasses against ASeinActor;
	// Abilities read AbilityRoots/ExcludedAbilities against USeinAbility. The walk below is identical.
	const bool bAbilities = (TargetKind == ESeinBalanceTargetKind::Abilities);
	UClass* const BaseClass = bAbilities ? USeinAbility::StaticClass() : ASeinActor::StaticClass();

	TArray<UClass*> RootClasses, ExcludedRootClasses;
	if (bAbilities)
	{
		for (const TSoftClassPtr<USeinAbility>& R : AbilityRoots)     { if (UClass* C = R.LoadSynchronous()) RootClasses.Add(C); }
		for (const TSoftClassPtr<USeinAbility>& E : ExcludedAbilities) { if (UClass* C = E.LoadSynchronous()) ExcludedRootClasses.Add(C); }
	}
	else
	{
		for (const TSoftClassPtr<ASeinActor>& R : IncludedRoots)   { if (UClass* C = R.LoadSynchronous()) RootClasses.Add(C); }
		for (const TSoftClassPtr<ASeinActor>& E : ExcludedClasses) { if (UClass* C = E.LoadSynchronous()) ExcludedRootClasses.Add(C); }
	}

	// Excluded = each excluded class + its whole subtree. Built first so the
	// included traversal can prune excluded branches (and we post-filter too).
	TSet<FTopLevelAssetPath> Excluded;
	for (UClass* ExClass : ExcludedRootClasses)
	{
		const FTopLevelAssetPath ExPath = ExClass->GetClassPathName();
		Excluded.Add(ExPath);
		AR.GetDerivedClassNames({ ExPath }, TSet<FTopLevelAssetPath>(), Excluded);
	}

	// Included = each root + its subtree, with excluded branches pruned.
	TSet<FTopLevelAssetPath> Included;
	for (UClass* RootClass : RootClasses)
	{
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
		if (!Cls || !Cls->IsChildOf(BaseClass)) continue;
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
		const int32 NameOrder = A.GetName().Compare(B.GetName());
		return NameOrder == 0
			? A.GetPathName() < B.GetPathName()
			: NameOrder < 0;
	});
}
#endif // WITH_EDITOR
