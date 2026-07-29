/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSimulationContentManifestBuilder.cpp
 */

#include "Util/SeinSimulationContentManifestBuilder.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryHelpers.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "IO/IoHash.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "SeinARTSCoreEntityModule.h"
#include "Serialization/SeinSimulationContentManifest.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Settings/PluginSettings.h"
#include "Simulation/SeinCanonicalStateRecipeRegistry.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/TopLevelAssetPath.h"
#include "UObject/UObjectIterator.h"
#include "Validators/SeinAbilityContinuationAnalysis.h"
#include "Validators/SeinContextFreeRecipeDeterminism.h"

DEFINE_LOG_CATEGORY_STATIC(
	LogSeinSimulationContentBuilder,
	Log,
	All);

namespace
{
	struct FManifestIdentity
	{
		FSoftObjectPath ObjectPath;
		FString ObjectPathString;
		FString PackageName;
		FName PackageFName;
		FName AssetName;
	};

	struct FExpectedManifestProfile
	{
		FManifestIdentity Manifest;
		FSeinSimulationContentManifestProfile Profile;
		TArray<FName> ContentPackages;
	};

	bool TryGetPackageMount(
		FName Package,
		FString& OutMount)
	{
		OutMount.Reset();
		const FString PackageString = Package.ToString();
		if (!PackageString.StartsWith(TEXT("/")))
		{
			return false;
		}

		const int32 MountEnd = PackageString.Find(
			TEXT("/"),
			ESearchCase::CaseSensitive,
			ESearchDir::FromStart,
			1);
		if (MountEnd <= 1)
		{
			return false;
		}

		OutMount = PackageString.Left(MountEnd);
		return true;
	}

	bool BuildAllowedContentMounts(
		TConstArrayView<FName> DirectPackages,
		const FSeinSimulationContentRegistrySnapshot& Snapshot,
		TSet<FString>& OutMounts,
		FString& OutError)
	{
		OutMounts.Reset();
		OutMounts.Add(TEXT("/Game"));
		for (const FName Package : DirectPackages)
		{
			FString Mount;
			if (!TryGetPackageMount(Package, Mount))
			{
				OutError = FString::Printf(
					TEXT("Direct simulation-content package '%s' has no canonical content mount."),
					*Package.ToString());
				return false;
			}
			if (Mount == TEXT("/Engine")
				|| Mount == TEXT("/Script"))
			{
				OutError = FString::Printf(
					TEXT("Direct simulation-content package '%s' belongs to host mount '%s'. Engine/native identity is covered by framework compatibility; author or register simulation content under a project or extension content mount."),
					*Package.ToString(),
					*Mount);
				return false;
			}
			OutMounts.Add(MoveTemp(Mount));
		}

		// A native discovery root opts in the content mount of the plugin that
		// owns its module. This remains registry-driven: merely enabling an
		// unrelated/test plugin does not make its assets part of a production
		// contributor profile.
		for (const FSeinSimulationContentDiscoveryRoot& Root :
			Snapshot.DiscoveryRoots)
		{
			const FTopLevelAssetPath RootPath(
				Root.RootClassPath);
			const FString RootPackage =
				RootPath.GetPackageName().ToString();
			if (RootPackage.StartsWith(TEXT("/Script/")))
			{
				const FString ModuleName =
					RootPackage.RightChop(
						FCString::Strlen(TEXT("/Script/")));
				const TSharedPtr<IPlugin> OwnerPlugin =
					IPluginManager::Get().
						GetModuleOwnerPlugin(
							FName(*ModuleName));
				if (OwnerPlugin.IsValid()
					&& OwnerPlugin->CanContainContent())
				{
					FString Mount =
						OwnerPlugin->GetMountedAssetPath();
					Mount.RemoveFromEnd(TEXT("/"));
					if (!Mount.IsEmpty())
					{
						OutMounts.Add(
							MoveTemp(Mount));
					}
				}
				continue;
			}

			FString Mount;
			if (!TryGetPackageMount(
				RootPath.GetPackageName(),
				Mount))
			{
				OutError = FString::Printf(
					TEXT("Registered discovery root '%s' has no canonical content mount."),
					*Root.RootClassPath);
				return false;
			}
			OutMounts.Add(MoveTemp(Mount));
		}
		return true;
	}

	bool ResolveManifestIdentity(
		const USeinARTSCoreSettings& Settings,
		FManifestIdentity& OutIdentity,
		FString& OutError)
	{
		OutIdentity = {};
		const FSoftObjectPath Path =
			Settings.SimulationContentManifest.ToSoftObjectPath();
		if (Path.IsNull()
			|| !Path.GetSubPathString().IsEmpty()
			|| !Path.GetAssetPath().IsValid())
		{
			OutError =
				TEXT("Configure a top-level Simulation Content Manifest asset path in Project Settings, then generate the manifest.");
			return false;
		}

		const FString PackageName = Path.GetLongPackageName();
		const FName AssetName = Path.GetAssetPath().GetAssetName();
		if (!FPackageName::IsValidLongPackageName(PackageName)
			|| AssetName.IsNone())
		{
			OutError = FString::Printf(
				TEXT("Configured Simulation Content Manifest path '%s' is not a valid top-level asset path."),
				*Path.ToString());
			return false;
		}

		OutIdentity.ObjectPath = Path;
		OutIdentity.ObjectPathString = Path.ToString();
		OutIdentity.PackageName = PackageName;
		OutIdentity.PackageFName = FName(*PackageName);
		OutIdentity.AssetName = AssetName;
		return true;
	}

	bool ValidateConfiguredRecipeBinding(
		const USeinARTSCoreSettings& Settings,
		FString& OutError)
	{
		FString BindingError;
		if (!FModuleManager::GetModuleChecked<
				FSeinARTSCoreEntity>(
					TEXT("SeinARTSCoreEntity"))
				.ValidateConfiguredCanonicalStateRecipes(
					BindingError))
		{
			OutError = FString::Printf(
				TEXT("Canonical-state recipe Project Settings do not match the claims installed at CoreEntity startup: %s Restart the editor after fixing the recipe list, then regenerate the Simulation Content Manifest."),
				BindingError.IsEmpty()
					? TEXT("the configured recipe binding is not ready.")
					: *BindingError);
			return false;
		}

		FString FreezeError;
		const FSeinCanonicalStateRecipeSnapshot Snapshot =
			FSeinCanonicalStateRecipeRegistry::Freeze(&FreezeError);
		if (!Snapshot.IsValid()
			|| !Snapshot.GetContractDigest().IsValid())
		{
			OutError = FString::Printf(
				TEXT("Canonical-state recipes are not ready: %s Restart the editor after fixing the recipe, then regenerate the Simulation Content Manifest."),
				FreezeError.IsEmpty()
					? TEXT("the live recipe registry could not freeze.")
					: *FreezeError);
			return false;
		}

		TMap<FString, int32> RegisteredPathCounts;
		for (const FSeinCanonicalStateRecipeDescriptor& Recipe :
			Snapshot.GetRecipes())
		{
			++RegisteredPathCounts.FindOrAdd(Recipe.RecipeClassPath);

			UClass* RecipeClass =
				FSoftClassPath(Recipe.RecipeClassPath)
					.TryLoadClass<USeinCanonicalStateRecipe>();
			if (!RecipeClass
				|| RecipeClass->GetPathName()
					!= Recipe.RecipeClassPath)
			{
				OutError = FString::Printf(
					TEXT("Registered canonical-state recipe '%s' cannot be resolved exactly as a USeinCanonicalStateRecipe class. Fix the provider, restart the editor, then regenerate the manifest."),
					*Recipe.RecipeClassPath);
				return false;
			}

			TArray<FText> RecipeDiagnostics;
			if (!SeinContextFreeRecipeDeterminism::ValidateClass(
				RecipeClass,
				RecipeDiagnostics))
			{
				TArray<FString> DiagnosticStrings;
				DiagnosticStrings.Reserve(
					RecipeDiagnostics.Num());
				for (const FText& Diagnostic : RecipeDiagnostics)
				{
					DiagnosticStrings.Add(
						Diagnostic.ToString());
				}
				OutError = FString::Printf(
					TEXT("Canonical-state recipe '%s' violates the context-free Blueprint contract:\n- %s\nFix the named graph/node/function, compile and save the Blueprint, then regenerate the Simulation Content Manifest."),
					*Recipe.RecipeClassPath,
					*FString::Join(
						DiagnosticStrings,
						TEXT("\n- ")));
				return false;
			}
		}

		TSet<FString> ConfiguredPaths;
		for (int32 Index = 0;
			Index < Settings.CanonicalStateRecipes.Num();
			++Index)
		{
			const TSoftClassPtr<USeinCanonicalStateRecipe>& Recipe =
				Settings.CanonicalStateRecipes[Index];
			const FString ExactPath =
				Recipe.ToSoftObjectPath().ToString();
			if (Recipe.IsNull() || ExactPath.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Canonical State Recipes[%d] is empty. Fix Project Settings, restart the editor, then regenerate the Simulation Content Manifest."),
					Index);
				return false;
			}
			if (ConfiguredPaths.Contains(ExactPath))
			{
				OutError = FString::Printf(
					TEXT("Canonical State Recipes contains duplicate exact path '%s'. Remove the duplicate, restart the editor, then regenerate the Simulation Content Manifest."),
					*ExactPath);
				return false;
			}
			ConfiguredPaths.Add(ExactPath);

			const int32 RegisteredCount =
				RegisteredPathCounts.FindRef(ExactPath);
			if (RegisteredCount != 1)
			{
				OutError = FString::Printf(
					TEXT("Configured canonical-state recipe '%s' has %d exact live registry entries. Project Settings changed after CoreEntity startup, or registration failed. Restart the editor, then regenerate the Simulation Content Manifest."),
					*ExactPath,
					RegisteredCount);
				return false;
			}

		}
		return true;
	}

	bool NormalizeSavedPackage(
		IAssetRegistry& AssetRegistry,
		const FString& Candidate,
		FName ManifestPackage,
		bool bDirectRoot,
		const FString& Context,
		FName& OutPackage,
		FString& OutError)
	{
		OutPackage = NAME_None;
		if (Candidate.StartsWith(TEXT("/Script/")))
		{
			if (bDirectRoot)
			{
				OutError = FString::Printf(
					TEXT("%s resolves to native script package '%s'; explicit simulation-content roots must be saved asset packages."),
					*Context,
					*Candidate);
				return false;
			}
			return true;
		}
		if (!FPackageName::IsValidLongPackageName(Candidate))
		{
			OutError = FString::Printf(
				TEXT("%s has invalid long package name '%s'."),
				*Context,
				*Candidate);
			return false;
		}

		FString CorrectCasePackageName;
		if (!AssetRegistry.DoesPackageExistOnDisk(
			FName(*Candidate),
			&CorrectCasePackageName))
		{
			OutError = FString::Printf(
				TEXT("%s package '%s' is unsaved or missing on disk."),
				*Context,
				*Candidate);
			return false;
		}

		OutPackage = FName(*CorrectCasePackageName);
		if (OutPackage == ManifestPackage)
		{
			if (bDirectRoot)
			{
				OutError = FString::Printf(
					TEXT("%s points at the generated manifest package itself. Remove that circular content root."),
					*Context);
				return false;
			}
			OutPackage = NAME_None;
		}
		return true;
	}

	bool AddDirectPackageRoot(
		IAssetRegistry& AssetRegistry,
		const FString& Candidate,
		FName ManifestPackage,
		const FString& Context,
		TArray<FName>& InOutQueue,
		TSet<FName>& InOutPackages,
		FString& OutError)
	{
		FName Package;
		if (!NormalizeSavedPackage(
			AssetRegistry,
			Candidate,
			ManifestPackage,
			true,
			Context,
			Package,
			OutError))
		{
			return false;
		}
		if (!Package.IsNone() && !InOutPackages.Contains(Package))
		{
			if (InOutPackages.Num()
				>= FSeinSimulationContentManifestCodec::MaxRecords)
			{
				OutError =
					TEXT("Simulation-content roots exceed the manifest record bound.");
				return false;
			}
			InOutPackages.Add(Package);
			InOutQueue.Add(Package);
		}
		return true;
	}

	bool AddDependencyPackage(
		IAssetRegistry& AssetRegistry,
		FName Candidate,
		FName ManifestPackage,
		FName Referencer,
		TArray<FName>& InOutQueue,
		TSet<FName>& InOutPackages,
		FString& OutError)
	{
		FName Package;
		if (!NormalizeSavedPackage(
			AssetRegistry,
			Candidate.ToString(),
			ManifestPackage,
			false,
			FString::Printf(
				TEXT("Dependency of '%s'"),
				*Referencer.ToString()),
			Package,
			OutError))
		{
			return false;
		}
		if (!Package.IsNone() && !InOutPackages.Contains(Package))
		{
			if (InOutPackages.Num()
				>= FSeinSimulationContentManifestCodec::MaxRecords)
			{
				OutError =
					TEXT("Simulation-content dependency closure exceeds the manifest record bound.");
				return false;
			}
			InOutPackages.Add(Package);
			InOutQueue.Add(Package);
		}
		return true;
	}

	bool AddDiscoveryRootPackages(
		IAssetRegistry& AssetRegistry,
		const FSeinSimulationContentRegistrySnapshot& Snapshot,
		FName ManifestPackage,
		const TSet<FString>& AllowedContentMounts,
		TArray<FName>& InOutQueue,
		TSet<FName>& InOutPackages,
		FString& OutError)
	{
		TArray<FTopLevelAssetPath> RootPaths;
		RootPaths.Reserve(Snapshot.DiscoveryRoots.Num());
		TSet<const UClass*> RootClasses;
		RootClasses.Reserve(Snapshot.DiscoveryRoots.Num());
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Resolving %d registered simulation-content class root(s)."),
			Snapshot.DiscoveryRoots.Num());

		for (const FSeinSimulationContentDiscoveryRoot& Root :
			Snapshot.DiscoveryRoots)
		{
			const FTopLevelAssetPath RootPath(Root.RootClassPath);
			if (!RootPath.IsValid() || RootPath.GetAssetName().IsNone())
			{
				OutError = FString::Printf(
					TEXT("Registered discovery root '%s' is not a valid class path."),
					*Root.RootClassPath);
				return false;
			}

			UClass* RootClass =
				FindObject<UClass>(nullptr, *Root.RootClassPath);
			if (!RootClass)
			{
				RootClass = LoadObject<UClass>(
					nullptr,
					*Root.RootClassPath,
					nullptr,
					LOAD_NoWarn | LOAD_Quiet);
			}
			if (!RootClass
				|| RootClass->GetPathName() != Root.RootClassPath)
			{
				OutError = FString::Printf(
					TEXT("Registered discovery root class '%s' cannot be resolved exactly."),
					*Root.RootClassPath);
				return false;
			}

			const FString RootPackage =
				RootPath.GetPackageName().ToString();
			if (!RootPackage.StartsWith(TEXT("/Script/"))
				&& !AddDirectPackageRoot(
					AssetRegistry,
					RootPackage,
					ManifestPackage,
					FString::Printf(
						TEXT("Discovery root '%s'"),
						*Root.RootClassPath),
					InOutQueue,
					InOutPackages,
					OutError))
			{
				return false;
			}
			RootPaths.Add(RootPath);
			RootClasses.Add(RootClass);
		}

		// Resolve the union once. Calling this per root repeats a hierarchy walk
		// and, more importantly, used to repeat the loaded-Blueprint scan below.
		TArray<FAssetData> DerivedClassAssets;
		UAssetRegistryHelpers::GetDerivedClassAssetData(
			RootPaths,
			DerivedClassAssets);
		DerivedClassAssets.Sort(
			[](const FAssetData& Left,
				const FAssetData& Right)
			{
				return Left.GetObjectPathString().Compare(
					Right.GetObjectPathString(),
					ESearchCase::CaseSensitive) < 0;
			});
		for (const FAssetData& Asset : DerivedClassAssets)
		{
			FString AssetMount;
			if (!TryGetPackageMount(
					Asset.PackageName,
					AssetMount)
				|| !AllowedContentMounts.Contains(
					AssetMount))
			{
				continue;
			}
			if (!AddDirectPackageRoot(
				AssetRegistry,
				Asset.PackageName.ToString(),
				ManifestPackage,
				FString::Printf(
					TEXT("Derived asset '%s'"),
					*Asset.GetObjectPathString()),
				InOutQueue,
				InOutPackages,
				OutError))
			{
				return false;
			}
		}

		// The Asset Registry hierarchy is saved-data based. Include live
		// Blueprint classes too so a new or reparented unsaved class cannot
		// disappear from the preflight merely because its tags are stale.
		TArray<const UBlueprint*> LoadedDerivedBlueprints;
		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			const UBlueprint* Blueprint = *It;
			if (!Blueprint
				|| !Blueprint->IsAsset()
				|| !Blueprint->GeneratedClass)
			{
				continue;
			}

			bool bMatchesRoot = false;
			for (const UClass* Class =
					Blueprint->GeneratedClass;
				Class;
				Class = Class->GetSuperClass())
			{
				if (RootClasses.Contains(Class))
				{
					bMatchesRoot = true;
					break;
				}
			}
			if (!bMatchesRoot)
			{
				continue;
			}
			LoadedDerivedBlueprints.Add(Blueprint);
		}
		LoadedDerivedBlueprints.Sort(
			[](const UBlueprint& Left,
				const UBlueprint& Right)
			{
				return Left.GetPathName().Compare(
					Right.GetPathName(),
					ESearchCase::CaseSensitive) < 0;
			});
		for (const UBlueprint* Blueprint :
			LoadedDerivedBlueprints)
		{
			FString BlueprintMount;
			if (!TryGetPackageMount(
					Blueprint->GetOutermost()->GetFName(),
					BlueprintMount)
				|| !AllowedContentMounts.Contains(
					BlueprintMount))
			{
				continue;
			}
			if (!AddDirectPackageRoot(
				AssetRegistry,
				Blueprint->GetOutermost()->GetName(),
				ManifestPackage,
				FString::Printf(
					TEXT("Loaded derived Blueprint '%s'"),
					*Blueprint->GetPathName()),
				InOutQueue,
				InOutPackages,
				OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool AddConfiguredPackageRoots(
		IAssetRegistry& AssetRegistry,
		const USeinARTSCoreSettings& Settings,
		const FSeinSimulationContentRegistrySnapshot& Snapshot,
		FName ManifestPackage,
		TArray<FName>& InOutQueue,
		TSet<FName>& InOutPackages,
		FString& OutError)
	{
		const FSoftObjectPath DefaultGameplayMap =
			Settings.DefaultGameplayMap.ToSoftObjectPath();
		if (!DefaultGameplayMap.IsNull())
		{
			if (!DefaultGameplayMap.GetSubPathString().IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Default Gameplay Map '%s' is not a top-level map asset."),
					*DefaultGameplayMap.ToString());
				return false;
			}
			const FAssetData MapAsset =
				AssetRegistry.GetAssetByObjectPath(
					DefaultGameplayMap,
					false,
					false);
			if (!MapAsset.IsValid()
				|| !MapAsset.IsInstanceOf(
					UWorld::StaticClass(),
					EResolveClass::Yes))
			{
				OutError = FString::Printf(
					TEXT("Default Gameplay Map path '%s' is missing or is not a UWorld asset."),
					*DefaultGameplayMap.ToString());
				return false;
			}
			if (!AddDirectPackageRoot(
				AssetRegistry,
				DefaultGameplayMap.GetLongPackageName(),
				ManifestPackage,
				TEXT("Default Gameplay Map"),
				InOutQueue,
				InOutPackages,
				OutError))
			{
				return false;
			}
		}

		for (int32 Index = 0;
			Index < Settings.AvailableMaps.Num();
			++Index)
		{
			const FSoftObjectPath MapPath =
				Settings.AvailableMaps[Index].Map.ToSoftObjectPath();
			if (MapPath.IsNull()
				|| !MapPath.GetSubPathString().IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Available Maps[%d] has no valid top-level map asset."),
					Index);
				return false;
			}

			const FAssetData MapAsset =
				AssetRegistry.GetAssetByObjectPath(
					MapPath,
					false,
					false);
			if (!MapAsset.IsValid()
				|| !MapAsset.IsInstanceOf(
					UWorld::StaticClass(),
					EResolveClass::Yes))
			{
				OutError = FString::Printf(
					TEXT("Available Maps[%d] path '%s' is missing or is not a UWorld asset."),
					Index,
					*MapPath.ToString());
				return false;
			}
			if (!AddDirectPackageRoot(
				AssetRegistry,
				MapPath.GetLongPackageName(),
				ManifestPackage,
				FString::Printf(TEXT("Available Maps[%d]"), Index),
				InOutQueue,
				InOutPackages,
				OutError))
			{
				return false;
			}
		}

		for (int32 Index = 0;
			Index < Settings.AdditionalSimulationContentRoots.Num();
			++Index)
		{
			const FSoftObjectPath& Root =
				Settings.AdditionalSimulationContentRoots[Index];
			if (Root.IsNull())
			{
				OutError = FString::Printf(
					TEXT("Additional Simulation Content Roots[%d] is empty."),
					Index);
				return false;
			}
			const FAssetData RootAsset =
				AssetRegistry.GetAssetByObjectPath(
					FSoftObjectPath(Root.GetAssetPath()),
					false,
					false);
			if (!RootAsset.IsValid())
			{
				OutError = FString::Printf(
					TEXT("Additional Simulation Content Roots[%d] top-level asset '%s' is missing."),
					Index,
					*Root.GetAssetPath().ToString());
				return false;
			}
			if (!AddDirectPackageRoot(
					AssetRegistry,
					Root.GetLongPackageName(),
					ManifestPackage,
					FString::Printf(
						TEXT("Additional Simulation Content Roots[%d]"),
						Index),
					InOutQueue,
					InOutPackages,
					OutError))
			{
				return false;
			}
		}

		for (int32 Index = 0;
			Index < Snapshot.ExplicitPackageRoots.Num();
			++Index)
		{
			if (!AddDirectPackageRoot(
				AssetRegistry,
				Snapshot.ExplicitPackageRoots[Index],
				ManifestPackage,
				FString::Printf(
					TEXT("Registered explicit package root[%d]"),
					Index),
				InOutQueue,
				InOutPackages,
				OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool AddDeterministicStructPackages(
		IAssetRegistry& AssetRegistry,
		FName ManifestPackage,
		const TSet<FString>& AllowedContentMounts,
		TArray<FName>& InOutQueue,
		TSet<FName>& InOutPackages,
		FString& OutError)
	{
		TArray<FAssetData> StructAssets;
		FARFilter StructFilter;
		StructFilter.ClassPaths.Add(
			UUserDefinedStruct::StaticClass()->GetClassPathName());
		StructFilter.bRecursivePaths = true;
		for (const FString& Mount : AllowedContentMounts)
		{
			StructFilter.PackagePaths.Add(FName(*Mount));
		}
		AssetRegistry.GetAssets(StructFilter, StructAssets);
		StructAssets.Sort(
			[](const FAssetData& Left,
				const FAssetData& Right)
			{
				return Left.GetObjectPathString().Compare(
					Right.GetObjectPathString(),
					ESearchCase::CaseSensitive) < 0;
			});
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Inspecting %d User Defined Struct asset(s) inside registered authored-content mounts."),
			StructAssets.Num());
		for (const FAssetData& Asset : StructAssets)
		{
			UUserDefinedStruct* Struct =
				Cast<UUserDefinedStruct>(Asset.GetAsset());
			if (!Struct)
			{
				OutError = FString::Printf(
					TEXT("User Defined Struct '%s' could not be loaded for simulation-content discovery."),
					*Asset.GetObjectPathString());
				return false;
			}
			if (!Struct->HasMetaData(TEXT("SeinDeterministic")))
			{
				continue;
			}
			if (Struct->Status != UDSS_UpToDate)
			{
				OutError = FString::Printf(
					TEXT("SeinDeterministic struct '%s' is uncompiled or has compile errors. Compile and save it before regenerating the manifest."),
					*Struct->GetPathName());
				return false;
			}
			if (!AddDirectPackageRoot(
				AssetRegistry,
				Asset.PackageName.ToString(),
				ManifestPackage,
				FString::Printf(
					TEXT("SeinDeterministic struct '%s'"),
					*Struct->GetPathName()),
				InOutQueue,
				InOutPackages,
				OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool BuildDependencyClosure(
		IAssetRegistry& AssetRegistry,
		FName ManifestPackage,
		const TSet<FString>& AllowedContentMounts,
		TArray<FName>& InOutQueue,
		TSet<FName>& InOutPackages,
		FString& OutError)
	{
		InOutQueue.Sort(FNameLexicalLess());
		for (int32 QueueIndex = 0;
			QueueIndex < InOutQueue.Num();
			++QueueIndex)
		{
			const FName Package = InOutQueue[QueueIndex];
			TArray<FName> Dependencies;
			AssetRegistry.GetDependencies(
				Package,
				Dependencies,
				UE::AssetRegistry::EDependencyCategory::Package,
				UE::AssetRegistry::EDependencyQuery::Game);
			Dependencies.Sort(FNameLexicalLess());
			for (const FName Dependency : Dependencies)
			{
				FString DependencyMount;
				if (TryGetPackageMount(
						Dependency,
						DependencyMount)
					&& !AllowedContentMounts.Contains(
						DependencyMount))
				{
					continue;
				}
				if (!AddDependencyPackage(
					AssetRegistry,
					Dependency,
					ManifestPackage,
					Package,
					InOutQueue,
					InOutPackages,
					OutError))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool BuildPackageRecord(
		IAssetRegistry& AssetRegistry,
		FName Package,
		bool bValidateUnloadedAssets,
		FSeinSimulationContentRecord& OutRecord,
		FString& OutCanonicalPackageName,
		FString& OutError)
	{
		OutRecord = {};
		OutCanonicalPackageName.Reset();

		FString CorrectCasePackageName;
		if (!AssetRegistry.DoesPackageExistOnDisk(
			Package,
			&CorrectCasePackageName))
		{
			OutError = FString::Printf(
				TEXT("Simulation-content package '%s' is unsaved or missing."),
				*Package.ToString());
			return false;
		}

		UPackage* LoadedPackage =
			FindPackage(nullptr, *CorrectCasePackageName);
		if (LoadedPackage && LoadedPackage->IsDirty())
		{
			OutError = FString::Printf(
				TEXT("Simulation-content package '%s' has unsaved changes. Compile and save all inputs before generating or starting PIE."),
				*CorrectCasePackageName);
			return false;
		}

		TArray<FAssetData> PackageAssets;
		AssetRegistry.GetAssetsByPackageName(
			FName(*CorrectCasePackageName),
			PackageAssets,
			false,
			false);
		for (const FAssetData& Asset : PackageAssets)
		{
			const bool bValidateAsset =
				bValidateUnloadedAssets
				|| Asset.IsAssetLoaded();
			if (Asset.IsInstanceOf(
				UBlueprint::StaticClass(),
				EResolveClass::Yes)
				&& bValidateAsset)
			{
				const UBlueprint* Blueprint =
					Cast<UBlueprint>(Asset.GetAsset());
				if (!Blueprint || !Blueprint->IsUpToDate())
				{
					OutError = FString::Printf(
						TEXT("Blueprint '%s' is uncompiled or has compile errors. Compile and save it before regenerating the manifest."),
						*Asset.GetObjectPathString());
					return false;
				}

				TArray<FSeinAbilityContinuationFinding> Findings;
				FSeinAbilityContinuationAnalysis::Analyze(
					*Blueprint,
					Findings);
				if (!Findings.IsEmpty())
				{
					constexpr int32 MaxReportedFindings = 8;
					TArray<FString> Diagnostics;
					const int32 ReportedCount = FMath::Min(
						Findings.Num(),
						MaxReportedFindings);
					Diagnostics.Reserve(ReportedCount);
					for (int32 Index = 0;
						Index < ReportedCount;
						++Index)
					{
						Diagnostics.Add(
							Findings[Index].ToDiagnostic());
					}
					const FString OmittedSuffix =
						Findings.Num() > ReportedCount
						? FString::Printf(
							TEXT("\n- ... and %d more finding(s)."),
							Findings.Num() - ReportedCount)
						: FString();
					OutError = FString::Printf(
						TEXT("Ability Blueprint '%s' violates the deterministic Move To continuation contract. Persist future-needed values in deterministic ability state before downstream async boundaries, then compile and save the Blueprint:\n- %s%s"),
						*Asset.GetObjectPathString(),
						*FString::Join(
							Diagnostics,
							TEXT("\n- ")),
						*OmittedSuffix);
					return false;
				}
			}
			else if (Asset.IsInstanceOf(
				UUserDefinedStruct::StaticClass(),
				EResolveClass::Yes)
				&& bValidateAsset)
			{
				const UUserDefinedStruct* Struct =
					Cast<UUserDefinedStruct>(Asset.GetAsset());
				if (!Struct || Struct->Status != UDSS_UpToDate)
				{
					OutError = FString::Printf(
						TEXT("User Defined Struct '%s' is uncompiled or has compile errors. Compile and save it before regenerating the manifest."),
						*Asset.GetObjectPathString());
					return false;
				}
			}
		}

		LoadedPackage = FindPackage(
			nullptr,
			*CorrectCasePackageName);
		if (LoadedPackage && LoadedPackage->IsDirty())
		{
			OutError = FString::Printf(
				TEXT("Simulation-content package '%s' became dirty while validating. Save it before regenerating the manifest."),
				*CorrectCasePackageName);
			return false;
		}

		const TOptional<FAssetPackageData> PackageData =
			AssetRegistry.GetAssetPackageDataCopy(
				FName(*CorrectCasePackageName));
		if (!PackageData.IsSet())
		{
			OutError = FString::Printf(
				TEXT("Asset Registry has no saved package data for '%s'. Rescan or save the package, then regenerate."),
				*CorrectCasePackageName);
			return false;
		}
		const FIoHash PackageSavedHash =
			PackageData->GetPackageSavedHash();
		if (PackageSavedHash.IsZero())
		{
			OutError = FString::Printf(
				TEXT("Package '%s' has no PackageSavedHash. Resave it with Unreal Engine 5.7 before regenerating the manifest."),
				*CorrectCasePackageName);
			return false;
		}
		static_assert(
			sizeof(FIoHash::ByteArray)
				== FSeinSimulationContentManifestCodec::
					SavedPackageHashBytes);

		OutRecord.StableRecordKindId =
			FSeinSimulationContentManifestCodec::
				GetCurrentRecordKindId();
		OutRecord.RecordRevision =
			static_cast<int32>(
				FSeinSimulationContentManifestCodec::
					CurrentRecordRevision);
		OutRecord.CanonicalRecordId =
			CorrectCasePackageName;
		if (!FSeinSimulationContentManifestCodec::
			ComputeRecordDigest(
				OutRecord.StableRecordKindId,
				static_cast<uint32>(
					OutRecord.RecordRevision),
				OutRecord.CanonicalRecordId,
				TConstArrayView<uint8>(
					PackageSavedHash.GetBytes(),
					sizeof(FIoHash::ByteArray)),
				OutRecord.ContentDigest,
				OutError))
		{
			return false;
		}

		OutCanonicalPackageName =
			CorrectCasePackageName;
		return true;
	}

	bool BuildExpectedProfile(
		FExpectedManifestProfile& OutExpected,
		FString& OutError)
	{
		OutExpected = {};
		OutError.Reset();
		check(IsInGameThread());
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Beginning Simulation Content Manifest profile generation."));

		const USeinARTSCoreSettings* Settings =
			GetDefault<USeinARTSCoreSettings>();
		if (!Settings)
		{
			OutError =
				TEXT("SeinARTS Core Settings are unavailable.");
			return false;
		}
		if (!ResolveManifestIdentity(
			*Settings,
			OutExpected.Manifest,
			OutError)
			|| !ValidateConfiguredRecipeBinding(
				*Settings,
				OutError))
		{
			return false;
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Validated manifest identity and canonical-state recipe binding."));

		IAssetRegistry& AssetRegistry =
			IAssetRegistry::GetChecked();
		if (AssetRegistry.IsSearchAllAssets())
		{
			AssetRegistry.WaitForCompletion();
		}
		else
		{
			AssetRegistry.SearchAllAssets(true);
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Asset Registry scan is complete."));

		FSeinSimulationContentRegistrySnapshot Snapshot;
		if (!FSeinSimulationContentRegistry::CaptureSnapshot(
			Snapshot,
			OutError))
		{
			return false;
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Captured %d simulation-content contributor(s), %d class root(s), and %d explicit package root(s)."),
			Snapshot.Contributors.Num(),
			Snapshot.DiscoveryRoots.Num(),
			Snapshot.ExplicitPackageRoots.Num());

		if (!FSeinSimulationContentRegistry::
			BuildManifestContributorRecords(
				Snapshot,
				OutExpected.Profile.Contributors,
				OutError))
		{
			return false;
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Canonicalized simulation-content contributor records."));

		TArray<FName> PackageQueue;
		TSet<FName> Packages;
		TSet<FString> AllowedContentMounts;
		if (!AddConfiguredPackageRoots(
				AssetRegistry,
				*Settings,
				Snapshot,
				OutExpected.Manifest.PackageFName,
				PackageQueue,
				Packages,
				OutError)
			|| !BuildAllowedContentMounts(
				PackageQueue,
				Snapshot,
				AllowedContentMounts,
				OutError))
		{
			return false;
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Simulation-content configured roots produced %d direct package(s) across %d authored-content mount(s)."),
			Packages.Num(),
			AllowedContentMounts.Num());

		if (!AddDiscoveryRootPackages(
			AssetRegistry,
			Snapshot,
			OutExpected.Manifest.PackageFName,
			AllowedContentMounts,
			PackageQueue,
			Packages,
			OutError))
		{
			return false;
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Simulation-content class discovery seeded %d direct package(s)."),
			Packages.Num());

		if (!AddDeterministicStructPackages(
			AssetRegistry,
			OutExpected.Manifest.PackageFName,
			AllowedContentMounts,
			PackageQueue,
			Packages,
			OutError))
		{
			return false;
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Simulation-content deterministic-struct discovery produced %d direct package(s)."),
			Packages.Num());

		const TSet<FName> DirectPackages = Packages;
		if (!BuildDependencyClosure(
			AssetRegistry,
			OutExpected.Manifest.PackageFName,
			AllowedContentMounts,
			PackageQueue,
			Packages,
			OutError))
		{
			return false;
		}
		UE_LOG(
			LogSeinSimulationContentBuilder,
			Display,
			TEXT("Simulation-content dependency closure resolved %d package(s) across %d registered authored-content mount(s)."),
			Packages.Num(),
			AllowedContentMounts.Num());
		if (Packages.IsEmpty())
		{
			OutError =
				TEXT("Simulation-content discovery produced no saved packages.");
			return false;
		}

		OutExpected.Profile.BuilderRevision =
			static_cast<int32>(
				FSeinSimulationContentManifestCodec::
					CurrentBuilderRevision);
		OutExpected.Profile.Records.Reserve(Packages.Num());
		OutExpected.ContentPackages.Reserve(Packages.Num());
		TArray<FName> SortedPackages = Packages.Array();
		SortedPackages.Sort(FNameLexicalLess());
		int32 RecordIndex = 0;
		for (const FName Package : SortedPackages)
		{
			if ((RecordIndex % 64) == 0)
			{
				UE_LOG(
					LogSeinSimulationContentBuilder,
					Display,
					TEXT("Validating and hashing simulation-content package %d of %d."),
					RecordIndex + 1,
					Packages.Num());
			}
			FString CanonicalPackageName;
			FSeinSimulationContentRecord& Record =
				OutExpected.Profile.Records
					.AddDefaulted_GetRef();
			if (!BuildPackageRecord(
				AssetRegistry,
				Package,
				DirectPackages.Contains(Package),
				Record,
				CanonicalPackageName,
				OutError))
			{
				return false;
			}
			OutExpected.ContentPackages.Add(
				FName(*CanonicalPackageName));
			++RecordIndex;
		}

		if (!FSeinSimulationContentManifestCodec::SealProfile(
			FSeinSimulationContentManifestCodec::
				CurrentFormatVersion,
			OutExpected.Profile,
			OutError))
		{
			return false;
		}
		OutExpected.ContentPackages.Reset(
			OutExpected.Profile.Records.Num());
		for (const FSeinSimulationContentRecord& Record :
			OutExpected.Profile.Records)
		{
			OutExpected.ContentPackages.Add(
				FName(*Record.CanonicalRecordId));
		}
		return true;
	}

	USeinSimulationContentManifest* LoadConfiguredManifest(
		const FManifestIdentity& Identity,
		FString& OutError)
	{
		UObject* Loaded = Identity.ObjectPath.TryLoad();
		USeinSimulationContentManifest* Manifest =
			Cast<USeinSimulationContentManifest>(Loaded);
		if (!Manifest
			|| Manifest->GetPathName()
				!= Identity.ObjectPathString)
		{
			OutError = FString::Printf(
				TEXT("Configured Simulation Content Manifest '%s' does not resolve exactly to a USeinSimulationContentManifest asset."),
				*Identity.ObjectPathString);
			return nullptr;
		}
		if (Manifest->GetOutermost()->IsDirty())
		{
			OutError = FString::Printf(
				TEXT("Generated manifest '%s' has unsaved changes. Regenerate and save it before starting PIE or cooking."),
				*Identity.ObjectPathString);
			return nullptr;
		}
		return Manifest;
	}

	USeinSimulationContentManifest* LoadOrCreateConfiguredManifest(
		const FManifestIdentity& Identity,
		bool& bOutCreated,
		FString& OutError)
	{
		bOutCreated = false;
		if (UObject* Existing = Identity.ObjectPath.ResolveObject())
		{
			USeinSimulationContentManifest* Manifest =
				Cast<USeinSimulationContentManifest>(Existing);
			if (!Manifest
				|| Manifest->GetPathName()
					!= Identity.ObjectPathString)
			{
				OutError = FString::Printf(
					TEXT("Object '%s' exists but is not the configured Simulation Content Manifest type."),
					*Identity.ObjectPathString);
			}
			return Manifest;
		}

		if (UObject* Existing = Identity.ObjectPath.TryLoad())
		{
			USeinSimulationContentManifest* Manifest =
				Cast<USeinSimulationContentManifest>(Existing);
			if (!Manifest
				|| Manifest->GetPathName()
					!= Identity.ObjectPathString)
			{
				OutError = FString::Printf(
					TEXT("Object '%s' exists but is not the configured Simulation Content Manifest type."),
					*Identity.ObjectPathString);
			}
			return Manifest;
		}

		UPackage* Package =
			CreatePackage(*Identity.PackageName);
		if (!Package)
		{
			OutError = FString::Printf(
				TEXT("Could not create manifest package '%s'."),
				*Identity.PackageName);
			return nullptr;
		}
		Package->FullyLoad();
		if (UObject* NameCollision =
			FindObject<UObject>(
				Package,
				*Identity.AssetName.ToString()))
		{
			OutError = FString::Printf(
				TEXT("Package '%s' already contains incompatible object '%s'."),
				*Identity.PackageName,
				*NameCollision->GetPathName());
			return nullptr;
		}

		USeinSimulationContentManifest* Manifest =
			NewObject<USeinSimulationContentManifest>(
				Package,
				USeinSimulationContentManifest::StaticClass(),
				Identity.AssetName,
				RF_Public | RF_Standalone | RF_Transactional);
		if (!Manifest)
		{
			OutError = FString::Printf(
				TEXT("Could not create manifest asset '%s'."),
				*Identity.ObjectPathString);
			return nullptr;
		}
		IAssetRegistry::GetChecked().AssetCreated(Manifest);
		bOutCreated = true;
		return Manifest;
	}

	void CopyBuildResult(
		const FExpectedManifestProfile& Expected,
		FSeinSimulationContentManifestBuildResult& OutResult)
	{
		OutResult.ManifestObjectPath =
			Expected.Manifest.ObjectPathString;
		OutResult.RootDigest =
			Expected.Profile.RootDigest;
		OutResult.ContributorCount =
			Expected.Profile.Contributors.Num();
		OutResult.RecordCount =
			Expected.Profile.Records.Num();
		OutResult.ContentPackages =
			Expected.ContentPackages;
	}
}

bool FSeinSimulationContentManifestBuilder::
	GenerateConfiguredManifest(
		FSeinSimulationContentManifestBuildResult& OutResult,
		FString& OutError)
{
	OutResult = {};
	OutError.Reset();
	FExpectedManifestProfile Expected;
	if (!BuildExpectedProfile(Expected, OutError))
	{
		return false;
	}

	bool bCreatedManifest = false;
	USeinSimulationContentManifest* Manifest =
		LoadOrCreateConfiguredManifest(
			Expected.Manifest,
			bCreatedManifest,
			OutError);
	if (!Manifest)
	{
		return false;
	}
	UPackage* Package = Manifest->GetOutermost();
	const bool bWasPackageDirty = Package->IsDirty();
	const TArray<FSeinSimulationContentManifestProfile>
		OriginalProfiles = Manifest->Profiles;
	Manifest->Modify();
	if (!FSeinSimulationContentManifestCodec::UpsertProfile(
		*Manifest,
		Expected.Profile,
		OutError))
	{
		Manifest->Profiles = OriginalProfiles;
		Package->SetDirtyFlag(
			bWasPackageDirty || bCreatedManifest);
		return false;
	}

	Package->MarkPackageDirty();
	const FString Filename =
		FPackageName::LongPackageNameToFilename(
			Expected.Manifest.PackageName,
			FPackageName::GetAssetPackageExtension());
	if (!IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(Filename),
		true))
	{
		Manifest->Profiles = OriginalProfiles;
		Package->SetDirtyFlag(
			bWasPackageDirty || bCreatedManifest);
		OutError = FString::Printf(
			TEXT("Could not create the generated manifest directory for '%s'. Check source control and file permissions."),
			*Filename);
		return false;
	}
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(
		Package,
		Manifest,
		*Filename,
		SaveArgs))
	{
		Manifest->Profiles = OriginalProfiles;
		Package->SetDirtyFlag(
			bWasPackageDirty || bCreatedManifest);
		OutError = FString::Printf(
			TEXT("Failed to save generated Simulation Content Manifest '%s'. Check source control and file permissions."),
			*Expected.Manifest.ObjectPathString);
		return false;
	}

	FSeinSimulationContentManifestProfile SavedProfile;
	if (!FSeinSimulationContentManifestCodec::SelectExactProfile(
		*Manifest,
		FSeinSimulationContentManifestCodec::
			CurrentBuilderRevision,
		Expected.Profile.Contributors,
		SavedProfile,
		OutError)
		|| !(SavedProfile == Expected.Profile))
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("The saved manifest profile does not match the generated profile.");
		}
		return false;
	}

	CopyBuildResult(Expected, OutResult);
	return true;
}

bool FSeinSimulationContentManifestBuilder::
	ValidateConfiguredManifest(
		FSeinSimulationContentManifestBuildResult& OutResult,
		FString& OutError)
{
	OutResult = {};
	OutError.Reset();
	FExpectedManifestProfile Expected;
	if (!BuildExpectedProfile(Expected, OutError))
	{
		return false;
	}

	USeinSimulationContentManifest* Manifest =
		LoadConfiguredManifest(
			Expected.Manifest,
			OutError);
	if (!Manifest)
	{
		return false;
	}

	FSeinSimulationContentManifestProfile StoredProfile;
	if (!FSeinSimulationContentManifestCodec::SelectExactProfile(
		*Manifest,
		FSeinSimulationContentManifestCodec::
			CurrentBuilderRevision,
		Expected.Profile.Contributors,
		StoredProfile,
		OutError))
	{
		OutError = FString::Printf(
			TEXT("%s Generate/regenerate the Simulation Content Manifest for the currently enabled plugin set."),
			*OutError);
		return false;
	}
	if (!(StoredProfile == Expected.Profile))
	{
		OutError = FString::Printf(
			TEXT("Simulation Content Manifest '%s' is stale: saved source packages or the discovered content graph changed. Regenerate it in Project Settings."),
			*Expected.Manifest.ObjectPathString);
		return false;
	}

	CopyBuildResult(Expected, OutResult);
	return true;
}
