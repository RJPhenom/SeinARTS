/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAssetDefinitions.cpp
 * @brief:		Implementation of the SeinARTS asset definitions.
 */

#include "Definitions/SeinAssetDefinitions.h"
#include "Actor/SeinActorBlueprint.h"
#include "Authoring/SeinEntityComponentBlueprint.h"
#include "Editors/SeinEntityComponentBlueprintEditor.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Effects/SeinEffectBlueprint.h"
#include "Formations/SeinFormationBlueprint.h"
#include "Balance/SeinBalanceProfile.h"
#include "BlueprintEditorModule.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "SBlueprintDiff.h"
#include "Toolkits/IToolkitHost.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

namespace
{
	// One FAssetCategoryPath per section, shared by every definition in that
	// section. The trailing FCategoryPath is marked Section so the entries
	// render under grey headers inside the SeinARTS flyout (like the engine's
	// Texture menu) instead of nested pull-out submenus.
	FAssetCategoryPath MakeSeinSection(const FText& SectionText)
	{
		return FAssetCategoryPath(
			FAssetCategoryPath(LOCTEXT("SeinARTSAssetCategory", "SeinARTS")),
			FCategoryPath(SectionText, ECategoryMenuType::Section));
	}

	TConstArrayView<FAssetCategoryPath> CoreSection()
	{
		static const TArray<FAssetCategoryPath, TFixedAllocator<1>> Categories = {
			MakeSeinSection(LOCTEXT("SeinCoreSection", "Core"))
		};
		return Categories;
	}

	TConstArrayView<FAssetCategoryPath> BehaviourPoliciesSection()
	{
		static const TArray<FAssetCategoryPath, TFixedAllocator<1>> Categories = {
			MakeSeinSection(LOCTEXT("SeinBehaviourPoliciesSection", "Behaviour Policies"))
		};
		return Categories;
	}

	TConstArrayView<FAssetCategoryPath> BalanceSection()
	{
		static const TArray<FAssetCategoryPath, TFixedAllocator<1>> Categories = {
			MakeSeinSection(LOCTEXT("SeinBalanceSection", "Balance"))
		};
		return Categories;
	}
}

// ==================== Shared Blueprint base ====================

EAssetCommandResult UAssetDefinition_SeinBlueprintBase::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	// Mirrors FAssetTypeActions_Blueprint::OpenAssetEditor — the behavior the
	// legacy asset actions inherited. UAssetDefinitionDefault must not be left
	// to handle this: its Unhandled fallback opens the generic property grid.
	const EToolkitMode::Type Mode = OpenArgs.GetToolkitMode();
	EAssetCommandResult Result = EAssetCommandResult::Unhandled;

	for (UBlueprint* Blueprint : OpenArgs.LoadObjects<UBlueprint>())
	{
		if (!Blueprint)
		{
			continue;
		}

		bool bLetOpen = true;
		if (!Blueprint->SkeletonGeneratedClass || !Blueprint->GeneratedClass)
		{
			bLetOpen = EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT(
				"FailedToLoadSeinBlueprintWithContinue",
				"Blueprint could not be loaded because it derives from an invalid class.\n"
				"Check to make sure the parent class for this blueprint hasn't been removed!\n"
				"Do you want to continue (it can crash the editor)?"));
		}

		if (bLetOpen)
		{
			FBlueprintEditorModule& BlueprintEditorModule =
				FModuleManager::LoadModuleChecked<FBlueprintEditorModule>("Kismet");
			BlueprintEditorModule.CreateBlueprintEditor(
				Mode, OpenArgs.ToolkitHost, Blueprint, ShouldUseDataOnlyEditor(Blueprint));
		}

		Result = EAssetCommandResult::Handled;
	}

	return Result;
}

EAssetCommandResult UAssetDefinition_SeinBlueprintBase::PerformAssetDiff(const FAssetDiffArgs& DiffArgs) const
{
	// Blueprint-aware revision diff (graphs + defaults), as the legacy actions
	// provided. The Movement definition's soft class can be unresolved when its
	// module is absent — no such assets exist then, but stay safe regardless.
	const UBlueprint* OldBlueprint = Cast<UBlueprint>(DiffArgs.OldAsset);
	const UBlueprint* NewBlueprint = Cast<UBlueprint>(DiffArgs.NewAsset);
	UClass* AssetClass = GetAssetClass().Get();
	SBlueprintDiff::CreateDiffWindow(
		OldBlueprint, NewBlueprint, DiffArgs.OldRevision, DiffArgs.NewRevision,
		AssetClass ? AssetClass : UBlueprint::StaticClass());
	return EAssetCommandResult::Handled;
}

bool UAssetDefinition_SeinBlueprintBase::ShouldUseDataOnlyEditor(const UBlueprint* Blueprint)
{
	return FBlueprintEditorUtils::IsDataOnlyBlueprint(Blueprint)
		&& !FBlueprintEditorUtils::IsLevelScriptBlueprint(Blueprint)
		&& !FBlueprintEditorUtils::IsInterfaceBlueprint(Blueprint)
		&& !Blueprint->bForceFullEditor
		&& !Blueprint->bIsNewlyCreated;
}

// ==================== Unit (SeinActorBlueprint) ====================

FText UAssetDefinition_SeinActorBlueprint::GetAssetDisplayName() const
{
	return LOCTEXT("SeinActorBlueprintName", "SeinARTS Entity Blueprint");
}

FLinearColor UAssetDefinition_SeinActorBlueprint::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("0095FF")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinActorBlueprint::GetAssetClass() const
{
	return USeinActorBlueprint::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinActorBlueprint::GetAssetCategories() const
{
	return CoreSection();
}

// ==================== Entity component (SeinEntityComponentBlueprint) ====================

FText UAssetDefinition_SeinEntityComponentBlueprint::GetAssetDisplayName() const
{
	return LOCTEXT("SeinEntityComponentBlueprintName", "SeinARTS Entity Component");
}

FLinearColor UAssetDefinition_SeinEntityComponentBlueprint::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("FF8000")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinEntityComponentBlueprint::GetAssetClass() const
{
	return USeinEntityComponentBlueprint::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinEntityComponentBlueprint::GetAssetCategories() const
{
	return CoreSection();
}

EAssetCommandResult UAssetDefinition_SeinEntityComponentBlueprint::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	const EToolkitMode::Type Mode = OpenArgs.GetToolkitMode();
	EAssetCommandResult Result = EAssetCommandResult::Unhandled;

	for (UBlueprint* Blueprint : OpenArgs.LoadObjects<UBlueprint>())
	{
		if (Blueprint)
		{
			TSharedRef<FSeinEntityComponentBlueprintEditor> Editor =
				MakeShared<FSeinEntityComponentBlueprintEditor>();
			Editor->InitEditor(Mode, OpenArgs.ToolkitHost, Blueprint);
			Result = EAssetCommandResult::Handled;
		}
	}

	return Result;
}

// ==================== Ability (SeinAbilityBlueprint) ====================

FText UAssetDefinition_SeinAbilityBlueprint::GetAssetDisplayName() const
{
	return LOCTEXT("SeinAbilityBlueprintName", "SeinARTS Ability");
}

FLinearColor UAssetDefinition_SeinAbilityBlueprint::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("FF0000")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinAbilityBlueprint::GetAssetClass() const
{
	return USeinAbilityBlueprint::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinAbilityBlueprint::GetAssetCategories() const
{
	return CoreSection();
}

// ==================== Effect (SeinEffectBlueprint) ====================

FText UAssetDefinition_SeinEffectBlueprint::GetAssetDisplayName() const
{
	return LOCTEXT("SeinEffectBlueprintName", "SeinARTS Effect");
}

FLinearColor UAssetDefinition_SeinEffectBlueprint::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("FF0000")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinEffectBlueprint::GetAssetClass() const
{
	return USeinEffectBlueprint::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinEffectBlueprint::GetAssetCategories() const
{
	return CoreSection();
}

// ==================== Formation (SeinFormationBlueprint) ====================

FText UAssetDefinition_SeinFormationBlueprint::GetAssetDisplayName() const
{
	return LOCTEXT("SeinFormationBlueprintName", "Formation Pattern");
}

FLinearColor UAssetDefinition_SeinFormationBlueprint::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("0095FF")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinFormationBlueprint::GetAssetClass() const
{
	return USeinFormationBlueprint::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinFormationBlueprint::GetAssetCategories() const
{
	return BehaviourPoliciesSection();
}

// ==================== Movement Mode (SeinMovementBlueprint) ====================

FText UAssetDefinition_SeinMovementBlueprint::GetAssetDisplayName() const
{
	return LOCTEXT("SeinMovementBlueprintName", "Movement Mode");
}

FLinearColor UAssetDefinition_SeinMovementBlueprint::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("0095FF")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinMovementBlueprint::GetAssetClass() const
{
	// Soft path — no link dependency on the Movement module.
	return TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Script/SeinARTSMovement.SeinMovementBlueprint")));
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinMovementBlueprint::GetAssetCategories() const
{
	return BehaviourPoliciesSection();
}

// ==================== Balance Profile (USeinBalanceProfile) ====================

FText UAssetDefinition_SeinBalanceProfile::GetAssetDisplayName() const
{
	return LOCTEXT("SeinBalanceProfileName", "Balance Data");
}

FLinearColor UAssetDefinition_SeinBalanceProfile::GetAssetColor() const
{
	return FLinearColor(FColor::FromHex(TEXT("B266FF")));
}

TSoftClassPtr<UObject> UAssetDefinition_SeinBalanceProfile::GetAssetClass() const
{
	return USeinBalanceProfile::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_SeinBalanceProfile::GetAssetCategories() const
{
	return BalanceSection();
}

#undef LOCTEXT_NAMESPACE
