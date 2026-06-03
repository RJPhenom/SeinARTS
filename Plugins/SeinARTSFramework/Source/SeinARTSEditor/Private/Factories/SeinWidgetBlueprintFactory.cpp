/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinWidgetBlueprintFactory.cpp
 * @date:		4/16/2026
 * @author:		RJ Macklem
 * @brief:		Implementation of SeinARTS Widget Blueprint factory.
 */

#include "Factories/SeinWidgetBlueprintFactory.h"
#include "Widgets/SeinWidgetBlueprint.h"
#include "SeinARTSEditorModule.h"
#include "Settings/PluginSettings.h"
#include "Dialogs/SSeinClassPickerDialog.h"
#include "Core/SeinUserWidget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "UMGEditorProjectSettings.h"
#include "UMGEditorModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SeinARTSEditor"

USeinWidgetBlueprintFactory::USeinWidgetBlueprintFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USeinWidgetBlueprint::StaticClass();
	ParentClass = USeinUserWidget::StaticClass();
	BlueprintType = BPTYPE_Normal;
}

bool USeinWidgetBlueprintFactory::ConfigureProperties()
{
	UClass* ChosenClass = SSeinClassPickerDialog::OpenDialog(
		LOCTEXT("PickWidgetParentClass", "Pick Parent Class for Widget"),
		USeinUserWidget::StaticClass(),
		LOCTEXT("GenericSeinWidget", "SeinARTS Widget"),
		LOCTEXT("GenericSeinWidgetTip", "Create a Widget Blueprint based on USeinUserWidget")
	);

	if (!ChosenClass)
	{
		return false;
	}

	ParentClass = ChosenClass;
	return true;
}

UObject* USeinWidgetBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Mirrors UWidgetBlueprintFactory::FactoryCreateNew (UE 5.7) but creates a
	// USeinWidgetBlueprint instead of a UWidgetBlueprint. Keeping the rest of
	// the engine's flow (root widget seed, bCanCallInitializedWithoutPlayerContext,
	// UMG broadcast) matters: external tooling hooks those signals.
	check(Class->IsChildOf(USeinWidgetBlueprint::StaticClass()));

	UClass* CurrentParentClass = ParentClass;
	if (CurrentParentClass == nullptr)
	{
		CurrentParentClass = USeinUserWidget::StaticClass();
	}

	if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(CurrentParentClass) || !CurrentParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ClassName"), FText::FromString(CurrentParentClass->GetName()));
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("CannotCreateSeinWidgetBlueprint", "Cannot create a SeinARTS Widget Blueprint based on the class '{ClassName}'."), Args));
		return nullptr;
	}

	USeinWidgetBlueprint* NewBP = CastChecked<USeinWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		CurrentParentClass, InParent, Name, BlueprintType,
		USeinWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass(),
		CallingContext
	));

	if (NewBP && NewBP->WidgetTree && NewBP->WidgetTree->RootWidget == nullptr)
	{
		TSubclassOf<UPanelWidget> RootWidgetPanel = GetDefault<UUMGEditorProjectSettings>()->DefaultRootWidget;
		if (!RootWidgetPanel)
		{
			RootWidgetPanel = UCanvasPanel::StaticClass();
		}

		UWidget* Root = NewBP->WidgetTree->ConstructWidget<UWidget>(RootWidgetPanel);
		NewBP->WidgetTree->RootWidget = Root;
		NewBP->OnVariableAdded(Root->GetFName());
	}

	if (NewBP)
	{
		NewBP->bCanCallInitializedWithoutPlayerContext = GetDefault<UUMGEditorProjectSettings>()->bCanCallInitializedWithoutPlayerContext;

		IUMGEditorModule::FWidgetBlueprintCreatedArgs BroadcastArgs;
		BroadcastArgs.ParentClass = CurrentParentClass;
		BroadcastArgs.Blueprint = NewBP;

		IUMGEditorModule& UMGEditor = FModuleManager::LoadModuleChecked<IUMGEditorModule>("UMGEditor");
		UMGEditor.OnWidgetBlueprintCreated().Broadcast(BroadcastArgs);
	}

	return NewBP;
}

UObject* USeinWidgetBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FactoryCreateNew(Class, InParent, Name, Flags, Context, Warn, NAME_None);
}

FText USeinWidgetBlueprintFactory::GetDisplayName() const
{
	return LOCTEXT("SeinWidgetBlueprintFactoryDisplayName", "View Model Widget");
}

uint32 USeinWidgetBlueprintFactory::GetMenuCategories() const
{
	uint32 Categories = FSeinARTSEditorModule::GetAssetCategoryBit();
	if (GetDefault<USeinARTSCoreSettings>()->bShowWidgetInBasicCategory)
	{
		Categories |= EAssetTypeCategories::Basic;
	}
	return Categories;
}

#undef LOCTEXT_NAMESPACE
