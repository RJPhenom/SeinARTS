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
#include "Abilities/SeinAbilityBlueprint.h"
#include "Effects/SeinEffectBlueprint.h"

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

#undef LOCTEXT_NAMESPACE
