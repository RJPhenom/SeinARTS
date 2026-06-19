/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementModeFactory.cpp
 */

#include "Factories/SeinMovementModeFactory.h"
#include "SeinARTSEditorModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinMovementModeFactory::USeinMovementModeFactory()
{
	SupportedClass = UBlueprint::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USeinMovementModeFactory::FactoryCreateNew(UClass* /*Class*/, UObject* InParent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	// Resolve USeinMovement by path — no link dependency on the Movement module.
	UClass* ParentClass = FindObject<UClass>(nullptr, TEXT("/Script/SeinARTSMovement.SeinMovement"));
	if (!ParentClass)
	{
		// Movement module unavailable (stripped / not loaded). Nothing sensible to parent to.
		return nullptr;
	}

	return FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		InParent,
		Name,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
}

FText USeinMovementModeFactory::GetDisplayName() const
{
	return LOCTEXT("SeinMovementModeFactoryDisplayName", "SeinARTS Movement Mode");
}

uint32 USeinMovementModeFactory::GetMenuCategories() const
{
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

FText USeinMovementModeFactory::GetToolTip() const
{
	return LOCTEXT("SeinMovementModeFactoryToolTip",
		"A movement mode Blueprint (child of Sein Movement). Override Compute Steer / Compute Desired "
		"Speed for custom feel, or the whole Tick for full control; add tuning variables and use the "
		"Class-Defaults 'Generate Tuning Data Structure' button to generate its tuning data.");
}

#undef LOCTEXT_NAMESPACE
