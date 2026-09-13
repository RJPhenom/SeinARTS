/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAbilityFactory.cpp
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Creates ability Blueprints and assigns their initial stable identities.
 * @disclaimer   Generated with assistance from an AI language model.
 */

#include "Factories/SeinAbilityFactory.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Util/SeinAutoTagGenerator.h"
#include "Abilities/SeinAbility.h"
#include "Abilities/SeinAbilityBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinAbilityFactory::USeinAbilityFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinAbilityBlueprint::StaticClass();
	ParentClass = USeinAbility::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

UObject* USeinAbilityFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Pre-creation collision check (see SeinEntityFactory comment for rationale).
	const USeinARTSCoreSettings* Settings = GetDefault<USeinARTSCoreSettings>();
	const FName ProposedTag = SeinAutoTag::ProposeTagName(Name, Settings);
	const FGameplayTag DerivedTag = FGameplayTag::RequestGameplayTag(ProposedTag, false);
	if (DerivedTag.IsValid())
	{
		const FString Collider = SeinAutoTag::FindCollidingAssetPath(DerivedTag);
		if (!Collider.IsEmpty())
		{
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
				LOCTEXT("AbilityTagCollisionMsg",
					"Cannot create '{0}' — its auto-generated tag '{1}' is already used by:\n\n{2}\n\nRename your new asset to produce a different tag."),
				FText::FromName(Name),
				FText::FromString(DerivedTag.ToString()),
				FText::FromString(Collider)));
			return nullptr;
		}
	}

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass, InParent, Name, BlueprintType,
		USeinAbilityBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		CallingContext
	);

	// Seed the event graph with the core ability lifecycle events so designers
	// get a starting point identical to how Actor BPs ship with BeginPlay/Tick.
	if (NewBP)
	{
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(NewBP);
		if (EventGraph)
		{
			int32 NodePosY = 0;
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnActivate")), USeinAbility::StaticClass(), NodePosY);
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnTick")),     USeinAbility::StaticClass(), NodePosY);
			FKismetEditorUtilities::AddDefaultEventNode(NewBP, EventGraph, FName(TEXT("OnEnd")),      USeinAbility::StaticClass(), NodePosY);
		}
	}

	// Preserve the native compatibility default for old assets, while new generic
	// abilities opt in to sharing deliberately. Children retain their parent policy.
	if (NewBP && NewBP->GeneratedClass && ParentClass == USeinAbility::StaticClass())
	{
		USeinAbility* CDO = CastChecked<USeinAbility>(NewBP->GeneratedClass->GetDefaultObject());
		CDO->CooldownScope = ESeinCooldownScope::OwnerOnly;
		FBlueprintEditorUtils::MarkBlueprintAsModified(NewBP);
	}

    if (NewBP && !ProposedTag.IsNone())
    {
        const auto Result = SeinAutoTag::InitializeNewAssetTag(NewBP);
        if (Result.IsFailure()) FMessageDialog::Open(EAppMsgType::Ok, Result.ToUserMessage(NewBP));
    }

	return NewBP;
}

bool USeinAbilityFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickAbilityParentClass", "Pick Parent Class for Ability"),
		USeinAbility::StaticClass(),
		LOCTEXT("GenericAbility", "Generic Ability"),
		LOCTEXT("GenericAbilityTip", "Create a Blueprint based on USeinAbility")
	);

	if (!ChosenClass)
	{
		return false;
	}

	ParentClass = ChosenClass;
	return true;
}

FText USeinAbilityFactory::GetDisplayName() const
{
	return LOCTEXT("SeinAbilityFactoryDisplayName", "SeinARTS Ability");
}

uint32 USeinAbilityFactory::GetMenuCategories() const
{
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowAbilityInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
