/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinAssetTypeActions.cpp
 * @date:		4/13/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of SeinARTS asset type actions.
 */

#include "SeinAssetTypeActions.h"
#include "SeinARTSEditorModule.h"
#include "Actor/SeinActorBlueprint.h"
#include "Authoring/SeinEntityComponentBlueprint.h"
#include "Editors/SeinEntityComponentBlueprintEditor.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Effects/SeinEffectBlueprint.h"
#include "Formations/SeinFormationBlueprint.h"
#include "Balance/SeinBalanceProfile.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

// ==================== Unit (SeinActorBlueprint) ====================

FText FAssetTypeActions_SeinActorBlueprint::GetName() const
{
	return LOCTEXT("SeinActorBlueprintName", "SeinARTS Entity Blueprint");
}

UClass* FAssetTypeActions_SeinActorBlueprint::GetSupportedClass() const
{
	return USeinActorBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinActorBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

// ==================== Entity component (SeinEntityComponentBlueprint) ====================

FText FAssetTypeActions_SeinEntityComponentBlueprint::GetName() const
{
	return LOCTEXT("SeinEntityComponentBlueprintName", "SeinARTS Entity Component");
}

UClass* FAssetTypeActions_SeinEntityComponentBlueprint::GetSupportedClass() const
{
	return USeinEntityComponentBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinEntityComponentBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

void FAssetTypeActions_SeinEntityComponentBlueprint::OpenAssetEditor(
	const TArray<UObject*>& InObjects,
	TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	const EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric : EToolkitMode::Standalone;
	for (UObject* Object : InObjects)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Object))
		{
			TSharedRef<FSeinEntityComponentBlueprintEditor> Editor =
				MakeShared<FSeinEntityComponentBlueprintEditor>();
			Editor->InitEditor(Mode, EditWithinLevelEditor, Blueprint);
		}
	}
}

// ==================== Ability (SeinAbilityBlueprint) ====================

FText FAssetTypeActions_SeinAbilityBlueprint::GetName() const
{
	return LOCTEXT("SeinAbilityBlueprintName", "SeinARTS Ability");
}

UClass* FAssetTypeActions_SeinAbilityBlueprint::GetSupportedClass() const
{
	return USeinAbilityBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinAbilityBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

// ==================== Effect (SeinEffectBlueprint) ====================

FText FAssetTypeActions_SeinEffectBlueprint::GetName() const
{
	return LOCTEXT("SeinEffectBlueprintName", "SeinARTS Effect");
}

UClass* FAssetTypeActions_SeinEffectBlueprint::GetSupportedClass() const
{
	return USeinEffectBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinEffectBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

// ==================== Formation (SeinFormationBlueprint) ====================

FText FAssetTypeActions_SeinFormationBlueprint::GetName() const
{
	return LOCTEXT("SeinFormationBlueprintName", "Formation Pattern");
}

UClass* FAssetTypeActions_SeinFormationBlueprint::GetSupportedClass() const
{
	return USeinFormationBlueprint::StaticClass();
}

uint32 FAssetTypeActions_SeinFormationBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

// ==================== Movement Mode (USeinMovementBlueprint) ====================

FText FAssetTypeActions_SeinMovementBlueprint::GetName() const
{
	return LOCTEXT("SeinMovementBlueprintName", "Movement Mode");
}

UClass* FAssetTypeActions_SeinMovementBlueprint::GetSupportedClass() const
{
	// Path-resolved — no link dependency on the Movement module. Registration in
	// SeinARTSEditorModule is gated on this class existing, so a registered
	// instance never resolves null here.
	static const TCHAR* Path = TEXT("/Script/SeinARTSMovement.SeinMovementBlueprint");
	return FindObject<UClass>(nullptr, Path);
}

uint32 FAssetTypeActions_SeinMovementBlueprint::GetCategories()
{
	return EAssetTypeCategories::Basic | FSeinARTSEditorModule::GetAssetCategoryBit();
}

// ==================== Balance Profile (USeinBalanceProfile) ====================

FText FAssetTypeActions_SeinBalanceProfile::GetName() const
{
	return LOCTEXT("SeinBalanceProfileName", "Balance Data");
}

UClass* FAssetTypeActions_SeinBalanceProfile::GetSupportedClass() const
{
	return USeinBalanceProfile::StaticClass();
}

uint32 FAssetTypeActions_SeinBalanceProfile::GetCategories()
{
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
