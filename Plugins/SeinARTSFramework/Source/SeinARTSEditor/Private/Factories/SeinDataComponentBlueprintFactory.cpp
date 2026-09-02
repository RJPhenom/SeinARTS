/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDataComponentBlueprintFactory.cpp
 */

#include "Factories/SeinDataComponentBlueprintFactory.h"
#include "SeinARTSEditorModule.h"

#include "Authoring/SeinDataComponent.h"
#include "Authoring/SeinEntityComponentBlueprint.h"
#include "Settings/PluginSettings.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "SeinDataComponentBlueprintFactory"

USeinDataComponentBlueprintFactory::USeinDataComponentBlueprintFactory()
{
	SupportedClass = USeinEntityComponentBlueprint::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USeinDataComponentBlueprintFactory::FactoryCreateNew(
	UClass* /*Class*/, UObject* InParent, FName Name, EObjectFlags Flags,
	UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return FKismetEditorUtilities::CreateBlueprint(
		USeinDataComponent::StaticClass(), InParent, Name,
		BPTYPE_Normal, USeinEntityComponentBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		TEXT("SeinDataComponentBlueprintFactory"));
}

FText USeinDataComponentBlueprintFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "SeinARTS Entity Component");
}

uint32 USeinDataComponentBlueprintFactory::GetMenuCategories() const
{
	// Inherits the legacy component factory's Basic-category settings toggle
	// (Editor Preferences → Factory Visibility → "Show SeinARTS Entity
	// Component in Basic Category").
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowComponentInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
