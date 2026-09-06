#include "CQTest.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Factories/SeinAbilityFactory.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::SeinARTSTests
{
	TEST(CooldownEnumMigrationAndExistingAssetAudit, "SeinARTS.Editor.CooldownSharing")
	{
		const UEnum* Enum = StaticEnum<ESeinCooldownScope>();
		ASSERT_THAT(AreEqual(static_cast<int64>(ESeinCooldownScope::OwnerOnly),
			Enum->GetValueByNameString(TEXT("ESeinCooldownScope::Member"))));
		ASSERT_THAT(AreEqual(static_cast<int64>(ESeinCooldownScope::SharedGroup),
			Enum->GetValueByNameString(TEXT("ESeinCooldownScope::Squad"))));
		ASSERT_THAT(IsTrue(GetDefault<USeinAbility>()->CooldownScope == ESeinCooldownScope::SharedGroup));
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.SearchAllAssets(true);
		TSet<FTopLevelAssetPath> Derived;
		Registry.GetDerivedClassNames({USeinAbility::StaticClass()->GetClassPathName()}, {}, Derived);
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets, true);
		int32 Audited = 0;
		for (const FAssetData& Asset : Assets)
		{
			FString GeneratedClass;
			if (!Asset.GetTagValue(FName(TEXT("GeneratedClass")), GeneratedClass)
				|| !Derived.Contains(FTopLevelAssetPath(FPackageName::ExportTextPathToObjectPath(GeneratedClass)))) continue;
			UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
			ASSERT_THAT(IsNotNull(Blueprint));
			ASSERT_THAT(IsNotNull(Blueprint->GeneratedClass));
			const USeinAbility* CDO = Cast<USeinAbility>(Blueprint->GeneratedClass->GetDefaultObject());
			ASSERT_THAT(IsNotNull(CDO));
			ASSERT_THAT(IsTrue(CDO->CooldownScope == ESeinCooldownScope::OwnerOnly
				|| CDO->CooldownScope == ESeinCooldownScope::SharedGroup));
			UE_LOG(LogTemp, Display, TEXT("[CooldownSharingAssetAudit] %s scope=%s"),
				*Asset.GetObjectPathString(), *Enum->GetNameStringByValue(static_cast<int64>(CDO->CooldownScope)));
			++Audited;
		}
		UE_LOG(LogTemp, Display, TEXT("[CooldownSharingAssetAudit] inspected=%d"), Audited);
	}

	TEST(NewGenericAbilityDefaultsPersistThroughCompileAndSave,
		"SeinARTS.Editor.CooldownSharing")
	{
		struct FCleanup
		{
			TArray<UPackage*> Packages;
			FString Filename;
			~FCleanup()
			{
				for (UPackage* Package : Packages) Package->SetDirtyFlag(false);
				if (!Filename.IsEmpty()) IFileManager::Get().Delete(*Filename);
			}
		} Cleanup;
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		UPackage* Package = CreatePackage(*(TEXT("/Temp/CooldownFactory_") + Suffix));
		Cleanup.Packages.Add(Package);
		TStrongObjectPtr<USeinAbilityFactory> Factory(NewObject<USeinAbilityFactory>());
		UBlueprint* Blueprint = Cast<UBlueprint>(Factory->FactoryCreateNew(
			USeinAbilityBlueprint::StaticClass(), Package, TEXT("CooldownFactory"),
			RF_Public | RF_Standalone, nullptr, GWarn, NAME_None));
		ASSERT_THAT(IsNotNull(Blueprint));
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		ASSERT_THAT(IsTrue(CastChecked<USeinAbility>(Blueprint->GeneratedClass->GetDefaultObject())->CooldownScope
			== ESeinCooldownScope::OwnerOnly));
		Cleanup.Filename = FPaths::ProjectSavedDir() / TEXT("Automation") / (TEXT("CooldownFactory_") + Suffix + TEXT(".uasset"));
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		ASSERT_THAT(IsTrue(UPackage::SavePackage(Package, Blueprint, *Cleanup.Filename, Args)));
		UPackage* Destination = CreatePackage(*(TEXT("/Temp/CooldownReload_") + Suffix));
		Cleanup.Packages.Add(Destination);
		UPackage* Reloaded = LoadPackage(Destination, *Cleanup.Filename, LOAD_None);
		ASSERT_THAT(IsNotNull(Reloaded));
		UBlueprint* ReloadedBP = FindObject<UBlueprint>(Reloaded, TEXT("CooldownFactory"));
		ASSERT_THAT(IsNotNull(ReloadedBP));
		ASSERT_THAT(IsTrue(CastChecked<USeinAbility>(ReloadedBP->GeneratedClass->GetDefaultObject())->CooldownScope
			== ESeinCooldownScope::OwnerOnly));
		// Choosing a parent with shared cooldown must preserve inherited policy.
		CastChecked<USeinAbility>(Blueprint->GeneratedClass->GetDefaultObject())->CooldownScope = ESeinCooldownScope::SharedGroup;
		Factory->ParentClass = Blueprint->GeneratedClass;
		UPackage* ChildPackage = CreatePackage(*(TEXT("/Temp/CooldownChild_") + Suffix));
		Cleanup.Packages.Add(ChildPackage);
		UBlueprint* Child = Cast<UBlueprint>(Factory->FactoryCreateNew(
			USeinAbilityBlueprint::StaticClass(), ChildPackage, TEXT("CooldownChild"),
			RF_Public | RF_Standalone, nullptr, GWarn, NAME_None));
		ASSERT_THAT(IsNotNull(Child));
		ASSERT_THAT(IsTrue(CastChecked<USeinAbility>(Child->GeneratedClass->GetDefaultObject())->CooldownScope
			== ESeinCooldownScope::SharedGroup));
	}
}
