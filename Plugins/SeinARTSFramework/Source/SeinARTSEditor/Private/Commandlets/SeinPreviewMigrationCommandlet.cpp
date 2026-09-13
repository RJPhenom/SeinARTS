/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPreviewMigrationCommandlet.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Saves and verifies demo preview component defaults and material instances.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Commandlets/SeinPreviewMigrationCommandlet.h"
#include "Targeter/SeinPointFacingTargeterPreview.h"
#include "Targeter/SeinPointTargeterPreview.h"
#include "Targeter/SeinTargeterVisualComponent.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"

namespace
{
	bool Save(UObject* Object)
	{
		UPackage* Package = Object->GetOutermost();
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, nullptr,
   *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), Args);
	}
	UMaterialInstanceConstant* SmokeMaterial(UMaterialInterface* Parent, const TCHAR* Suffix, FLinearColor Color, bool bApply)
	{
		const FString PackageName = FString(TEXT("/SeinARTSFramework/Demo/Materials/MAT_SmokeTargeter_")) + Suffix;
		const FString ObjectName = FPackageName::GetLongPackageAssetName(PackageName);
		auto* Result = LoadObject<UMaterialInstanceConstant>(nullptr, *(PackageName + TEXT(".") + ObjectName), nullptr, LOAD_NoWarn);
		if (bApply)
		{
			if (!Result)
			{
				Result = NewObject<UMaterialInstanceConstant>(CreatePackage(*PackageName), *ObjectName, RF_Public | RF_Standalone);
				FAssetRegistryModule::AssetCreated(Result);
			}
			Result->SetParentEditorOnly(Parent);
			Result->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("TargetColour")), Color);
			Result->PostEditChange();
			if (!Save(Result)) return nullptr;
		}
		if (!Result || Result->Parent != Parent) return nullptr;
		FLinearColor Actual;
		if (!Result->GetVectorParameterValue(FMaterialParameterInfo(TEXT("TargetColour")), Actual) || !Actual.Equals(Color)) return nullptr;
		return Result;
	}
}

USeinPreviewMigrationCommandlet::USeinPreviewMigrationCommandlet()
{
	IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}
int32 USeinPreviewMigrationCommandlet::Main(const FString& Params)
{
	const bool Apply = FParse::Param(*Params, TEXT("Apply"));
	auto* Building = LoadObject<UBlueprint>(nullptr, TEXT("/SeinARTSFramework/Demo/Blueprints/STP_BuildingHologram.STP_BuildingHologram"));
	auto* Smoke = LoadObject<UBlueprint>(nullptr, TEXT("/SeinARTSFramework/Demo/Blueprints/STP_SmokeTargeter.STP_SmokeTargeter"));
	auto* Valid = LoadObject<UMaterialInterface>(nullptr, TEXT("/SeinARTSFramework/Demo/Materials/MAT_Hologram_Valid.MAT_Hologram_Valid"));
	auto* Blocked = LoadObject<UMaterialInterface>(nullptr, TEXT("/SeinARTSFramework/Demo/Materials/MAT_Hologram_Invalid.MAT_Hologram_Invalid"));
	auto* Ring = LoadObject<UMaterialInterface>(nullptr, TEXT("/SeinARTSFramework/Materials/MAT_TargeterPreview.MAT_TargeterPreview"));
	FLinearColor SourceColor;
	if (!Building || !Smoke || !Valid || !Blocked || !Ring
		|| !Ring->GetVectorParameterValue(FMaterialParameterInfo(TEXT("TargetColour")), SourceColor))
	{
		UE_LOG(LogTemp, Error, TEXT("Preview migration preflight failed: missing asset or TargetColour parameter."));
		return 1;
	}
	if (Building->ParentClass != ASeinPointFacingTargeterPreview::StaticClass()
		|| Smoke->ParentClass != ASeinPointTargeterPreview::StaticClass()) return 2;
	for (auto* BP : {Building, Smoke})
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error || !BP->GeneratedClass) return 3;
	}
	auto* B = Cast<ASeinPointFacingTargeterPreview>(Building->GeneratedClass->GetDefaultObject());
	auto* S = Cast<ASeinPointTargeterPreview>(Smoke->GeneratedClass->GetDefaultObject());
	if (!B || !S || !B->MeshPreview || !S->DecalPreview) return 4;
	auto* SmokeValid = SmokeMaterial(Ring, TEXT("Valid"), SourceColor, Apply);
	auto* SmokeWarning = SmokeMaterial(Ring, TEXT("Warning"), FLinearColor(1, .85f, .2f, 1), Apply);
	auto* SmokeBlocked = SmokeMaterial(Ring, TEXT("Blocked"), FLinearColor(1, .2f, .2f, 1), Apply);
	if (!SmokeValid || !SmokeWarning || !SmokeBlocked) return 5;
	if (Apply)
	{
		B->Modify(); B->MeshPreview->Modify(); S->Modify(); S->DecalPreview->Modify();
		B->MeshPreview->Materials.Valid = Valid;
		B->MeshPreview->Materials.Blocked = Blocked;
		// No warning material selected: use the valid hologram, leaving construction's Placed material separate.
		B->MeshPreview->Materials.Warning = nullptr;
		S->DecalPreview->Materials.Valid = SmokeValid;
		S->DecalPreview->Materials.Warning = SmokeWarning;
		S->DecalPreview->Materials.Blocked = SmokeBlocked;
		S->DecalPreview->Radius = S->DefaultPointRadius;
		S->DecalPreview->ProjectionDepth = S->DecalHeight;
		FBlueprintEditorUtils::MarkBlueprintAsModified(Building);
		FBlueprintEditorUtils::MarkBlueprintAsModified(Smoke);
		if (!Save(Building) || !Save(Smoke)) return 6;
	}
	if (B->MeshPreview->Materials.Valid != Valid || B->MeshPreview->Materials.Blocked != Blocked
		|| S->DecalPreview->Materials.Valid != SmokeValid || S->DecalPreview->Materials.Blocked != SmokeBlocked
		|| S->DecalPreview->Materials.Warning != SmokeWarning) return 7;
	UE_LOG(LogTemp, Display, TEXT("PREVIEW_MIGRATION_VERIFIED: building and smoke defaults; unchanged parents; explicit validity materials."));
	return 0;
}
