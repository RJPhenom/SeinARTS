/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinFormationFactory.cpp
 * @brief:		Implementation of SeinFormation Blueprint factory.
 */

#include "Factories/SeinFormationFactory.h"
#include "SeinARTSEditorModule.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Formations/SeinFormation.h"
#include "Formations/SeinFormationBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/BlueprintGeneratedClass.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinFormationFactory::USeinFormationFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinFormationBlueprint::StaticClass();
	ParentClass = USeinFormation::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

UObject* USeinFormationFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Formations carry no gameplay tag and no event-graph events — the override surface is the
	// BuildFormation FUNCTION (reached via the Blueprint's Functions -> Override list), not events — so
	// creation is just the Blueprint itself. The resolver post-processes whatever BuildFormation returns
	// (de-overlap + nav-clamp + facing), so a designer's graph only needs to emit a rough shape.
	return FKismetEditorUtilities::CreateBlueprint(
		ParentClass, InParent, Name, BlueprintType,
		USeinFormationBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		CallingContext
	);
}

bool USeinFormationFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickFormationParentClass", "Pick Parent Class for Formation"),
		USeinFormation::StaticClass(),
		LOCTEXT("GenericFormation", "Generic Formation"),
		LOCTEXT("GenericFormationTip", "Create a Blueprint based on USeinFormation (or a stock formation to extend it)")
	);

	if (!ChosenClass)
	{
		return false;
	}

	ParentClass = ChosenClass;
	return true;
}

FText USeinFormationFactory::GetDisplayName() const
{
	return LOCTEXT("SeinFormationFactoryDisplayName", "Formation Pattern");
}

uint32 USeinFormationFactory::GetMenuCategories() const
{
	return FSeinARTSEditorModule::GetAssetCategoryBit();
}

#undef LOCTEXT_NAMESPACE
