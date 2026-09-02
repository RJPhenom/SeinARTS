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

namespace
{
	// Both classes are resolved by path — no link dependency on the Movement module.
	UClass* ResolveMovementParentClass()
	{
		return FindObject<UClass>(nullptr, TEXT("/Script/SeinARTSMovement.SeinMovement"));
	}

	UClass* ResolveMovementBlueprintClass()
	{
		return FindObject<UClass>(nullptr, TEXT("/Script/SeinARTSMovement.SeinMovementBlueprint"));
	}
}

USeinMovementModeFactory::USeinMovementModeFactory()
{
	// The dedicated asset class carries the Movement Mode type color/actions
	// (UFactory::GetSupportedClass is non-virtual, so the member must hold it).
	// SeinARTSMovement is a Default-phase runtime module and this editor module
	// loads PostEngineInit, so the path resolve succeeds at CDO construction;
	// plain UBlueprint stands in if the Movement module is ever absent.
	UClass* BlueprintClass = ResolveMovementBlueprintClass();
	SupportedClass = BlueprintClass ? BlueprintClass : UBlueprint::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USeinMovementModeFactory::FactoryCreateNew(UClass* /*Class*/, UObject* InParent, FName Name, EObjectFlags Flags, UObject* /*Context*/, FFeedbackContext* /*Warn*/)
{
	UClass* ParentClass = ResolveMovementParentClass();
	if (!ParentClass)
	{
		// Movement module unavailable (stripped / not loaded). Nothing sensible to parent to.
		return nullptr;
	}

	UClass* BlueprintClass = ResolveMovementBlueprintClass();
	return FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		InParent,
		Name,
		BPTYPE_Normal,
		BlueprintClass ? BlueprintClass : UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		NAME_None);
}

FText USeinMovementModeFactory::GetDisplayName() const
{
	return LOCTEXT("SeinMovementModeFactoryDisplayName", "Movement Mode");
}

uint32 USeinMovementModeFactory::GetMenuCategories() const
{
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

FText USeinMovementModeFactory::GetToolTip() const
{
	return LOCTEXT("SeinMovementModeFactoryToolTip",
		"A movement mode Blueprint (child of Sein Movement). Override Compute Steer / Compute Speed for "
		"custom feel, or the whole Tick for full control; add tuning variables and use the Class-Defaults "
		"'Generate Tuning Data Structure' button to generate its tuning data.");
}

#undef LOCTEXT_NAMESPACE
