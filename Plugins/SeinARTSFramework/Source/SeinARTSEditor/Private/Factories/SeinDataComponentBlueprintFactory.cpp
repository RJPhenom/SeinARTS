/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDataComponentBlueprintFactory.cpp
 */

#include "Factories/SeinDataComponentBlueprintFactory.h"
#include "SeinARTSEditorModule.h"

#include "Authoring/SeinDataComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "SeinDataComponentBlueprintFactory"

USeinDataComponentBlueprintFactory::USeinDataComponentBlueprintFactory()
{
	SupportedClass = UBlueprint::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USeinDataComponentBlueprintFactory::FactoryCreateNew(
	UClass* /*Class*/, UObject* InParent, FName Name, EObjectFlags Flags,
	UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	return FKismetEditorUtilities::CreateBlueprint(
		USeinDataComponent::StaticClass(), InParent, Name,
		BPTYPE_Normal, UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		TEXT("SeinDataComponentBlueprintFactory"));
}

FText USeinDataComponentBlueprintFactory::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "SeinARTS Data Component");
}

uint32 USeinDataComponentBlueprintFactory::GetMenuCategories() const
{
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
