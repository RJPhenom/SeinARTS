/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityComponentBlueprintEditor.cpp
 */

#include "Editors/SeinEntityComponentBlueprintEditor.h"

#include "Authoring/SeinDataComponent.h"
#include "Util/SeinDeterminismRules.h"

#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Framework/Docking/TabManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "SPinTypeSelector.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SeinEntityComponentBlueprintEditor"

const FName FSeinEntityComponentBlueprintEditor::VariablesTabId(TEXT("SeinEntityComponentEditor_Variables"));
const FName FSeinEntityComponentBlueprintEditor::DefaultsTabId(TEXT("SeinEntityComponentEditor_Defaults"));

namespace
{
	/** Whitelist filter applied AT SELECTION TIME: only deterministic pin
	 *  types appear in the picker at all. */
	class FSeinDeterministicPinFilter : public IPinTypeSelectorFilter
	{
	public:
		virtual bool ShouldShowPinTypeTreeItem(FPinTypeTreeItem InItem) const override
		{
			if (!InItem.IsValid())
			{
				return false;
			}
			// Category headers stay visible; leaf types must pass the shared
			// determinism rule (same predicate the UDS validator and the
			// payload sync apply).
			const FEdGraphPinType& PinType =
				InItem->GetPinType(/*bForceLoadedSubCategoryObject*/ false);
			if (PinType.PinCategory == NAME_None)
			{
				return true;
			}
			return SeinDeterminism::IsPinTypeDeterministic(PinType);
		}
	};
}

void FSeinEntityComponentBlueprintEditor::InitEditor(
	const EToolkitMode::Type Mode,
	const TSharedPtr<IToolkitHost>& InitToolkitHost,
	UBlueprint* InBlueprint)
{
	Blueprint = InBlueprint;

	const TSharedRef<FTabManager::FLayout> Layout =
		FTabManager::NewLayout("SeinEntityComponentBlueprintEditor_Layout_v2")
		->AddArea(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.22f)
				->AddTab(VariablesTabId, ETabState::OpenedTab))
			->Split(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.78f)
				->AddTab(DefaultsTabId, ETabState::OpenedTab)));

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost,
		TEXT("SeinEntityComponentBlueprintEditor"), Layout,
		/*bCreateDefaultStandaloneMenu*/ true,
		/*bCreateDefaultToolbar*/ true, InBlueprint);

	RefreshDefaultsView();
}

void FSeinEntityComponentBlueprintEditor::RegisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(VariablesTabId,
		FOnSpawnTab::CreateSP(this,
			&FSeinEntityComponentBlueprintEditor::SpawnVariablesTab))
		.SetDisplayName(LOCTEXT("VariablesTab", "Variables"));
	InTabManager->RegisterTabSpawner(DefaultsTabId,
		FOnSpawnTab::CreateSP(this,
			&FSeinEntityComponentBlueprintEditor::SpawnDefaultsTab))
		.SetDisplayName(LOCTEXT("DefaultsTab", "Defaults"));
}

void FSeinEntityComponentBlueprintEditor::UnregisterTabSpawners(
	const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(VariablesTabId);
	InTabManager->UnregisterTabSpawner(DefaultsTabId);
}

FName FSeinEntityComponentBlueprintEditor::GetToolkitFName() const
{
	return FName("SeinEntityComponentBlueprintEditor");
}

FText FSeinEntityComponentBlueprintEditor::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "Sein Entity Component");
}

FString FSeinEntityComponentBlueprintEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("SeinEntityComponent");
}

FLinearColor FSeinEntityComponentBlueprintEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FF8000")));
}

void FSeinEntityComponentBlueprintEditor::AddReferencedObjects(
	FReferenceCollector& Collector)
{
	// The toolkit's standard edited-objects list already references the
	// Blueprint; nothing extra to pin.
}

TSharedRef<SDockTab> FSeinEntityComponentBlueprintEditor::SpawnVariablesTab(
	const FSpawnTabArgs& /*Args*/)
{
	TSharedRef<SDockTab> Tab = SNew(SDockTab)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f)
		[
			SNew(SButton)
			.Text(LOCTEXT("AddVariable", "Add Variable"))
			.OnClicked_Lambda([this]()
			{
				OnAddVariable();
				return FReply::Handled();
			})
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(VariablesBox, SVerticalBox)
			]
		]
	];
	RefreshVariablesPanel();
	return Tab;
}

TSharedRef<SDockTab> FSeinEntityComponentBlueprintEditor::SpawnDefaultsTab(
	const FSpawnTabArgs& /*Args*/)
{
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs ViewArgs;
	ViewArgs.bHideSelectionTip = true;
	ViewArgs.bAllowSearch = true;
	DefaultsView = PropertyModule.CreateDetailView(ViewArgs);
	// This is not a "real" ActorComponent to the designer — tick, replication,
	// activation, cooking, and the rest of the UActorComponent surface are
	// meaningless on a data-only payload definition (and Is Editor Only is a
	// footgun). Show only what our classes declare: the component's variables
	// and the SeinARTS base fields.
	DefaultsView->SetIsPropertyVisibleDelegate(
		FIsPropertyVisible::CreateLambda([](const FPropertyAndParent& PropertyAndParent)
	{
		const FProperty* Outermost = PropertyAndParent.ParentProperties.Num() > 0
			? PropertyAndParent.ParentProperties.Last()
			: &PropertyAndParent.Property;
		const UClass* OwnerClass = Outermost->GetOwnerClass();
		return OwnerClass
			&& OwnerClass->IsChildOf(USeinDataComponent::StaticClass());
	}));
	RefreshDefaultsView();

	return SNew(SDockTab)
	[
		DefaultsView.ToSharedRef()
	];
}

void FSeinEntityComponentBlueprintEditor::RefreshVariablesPanel()
{
	if (!VariablesBox.IsValid())
	{
		return;
	}
	VariablesBox->ClearChildren();
	UBlueprint* BP = Blueprint.Get();
	if (!BP)
	{
		return;
	}
	if (BP->NewVariables.Num() == 0)
	{
		VariablesBox->AddSlot()
		.AutoHeight()
		.Padding(8.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoVariables",
				"No variables yet. Every variable you add becomes a field of this component's injected payload."))
			.AutoWrapText(true)
		];
		return;
	}
	for (const FBPVariableDescription& Variable : BP->NewVariables)
	{
		VariablesBox->AddSlot()
		.AutoHeight()
		.Padding(4.f, 2.f)
		[
			MakeVariableRow(Variable)
		];
	}
}

TSharedRef<SWidget> FSeinEntityComponentBlueprintEditor::MakeVariableRow(
	const FBPVariableDescription& Variable)
{
	const FGuid VarGuid = Variable.VarGuid;
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	TArray<TSharedPtr<IPinTypeSelectorFilter>> Filters;
	Filters.Add(MakeShared<FSeinDeterministicPinFilter>());

	return SNew(SBorder)
	.Padding(4.f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(0.45f)
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromName(Variable.VarName))
			.OnTextCommitted_Lambda(
				[this, VarGuid](const FText& NewText, ETextCommit::Type CommitType)
			{
				if (CommitType == ETextCommit::OnEnter
					|| CommitType == ETextCommit::OnUserMovedFocus)
				{
					OnRenameVariable(VarGuid, NewText);
				}
			})
		]
		+ SHorizontalBox::Slot()
		.FillWidth(0.45f)
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SPinTypeSelector,
				FGetPinTypeTree::CreateUObject(Schema,
					&UEdGraphSchema_K2::GetVariableTypeTree))
			.TargetPinType_Lambda([this, VarGuid]()
			{
				if (UBlueprint* BP = Blueprint.Get())
				{
					for (const FBPVariableDescription& Var : BP->NewVariables)
					{
						if (Var.VarGuid == VarGuid)
						{
							return Var.VarType;
						}
					}
				}
				return FEdGraphPinType();
			})
			.OnPinTypeChanged_Lambda([this, VarGuid](const FEdGraphPinType& NewType)
			{
				OnVariableTypeChanged(VarGuid, NewType);
			})
			.Schema(Schema)
			.CustomFilters(Filters)
			.TypeTreeFilter(ETypeTreeFilter::None)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveVariable", "X"))
			.ToolTipText(LOCTEXT("RemoveVariableTip", "Remove this variable (and its payload field)"))
			.OnClicked_Lambda([this, VarGuid]()
			{
				OnRemoveVariable(VarGuid);
				return FReply::Handled();
			})
		]
	];
}

void FSeinEntityComponentBlueprintEditor::RefreshDefaultsView()
{
	UBlueprint* BP = Blueprint.Get();
	if (!DefaultsView.IsValid() || !BP)
	{
		return;
	}
	UObject* CDO = BP->GeneratedClass
		? BP->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/ false)
		: nullptr;
	DefaultsView->SetObject(CDO, /*bForceRefresh*/ true);
}

void FSeinEntityComponentBlueprintEditor::CompileAndRefresh()
{
	UBlueprint* BP = Blueprint.Get();
	if (!BP || bCompiling)
	{
		return;
	}
	TGuardValue<bool> Guard(bCompiling, true);
	FKismetEditorUtilities::CompileBlueprint(BP);
	RefreshVariablesPanel();
	RefreshDefaultsView();
}

void FSeinEntityComponentBlueprintEditor::OnAddVariable()
{
	UBlueprint* BP = Blueprint.Get();
	if (!BP)
	{
		return;
	}
	const FScopedTransaction Transaction(
		LOCTEXT("AddVariableTransaction", "Add Component Variable"));

	// Default new variables to FixedPoint — the dominant deterministic sim
	// type — and make them instance-editable so per-placed-actor overrides
	// work out of the box.
	FEdGraphPinType FixedPointType;
	FixedPointType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	FixedPointType.PinSubCategoryObject =
		FindObject<UScriptStruct>(nullptr, TEXT("/Script/SeinARTSCore.FixedPoint"));
	if (!FixedPointType.PinSubCategoryObject.IsValid())
	{
		FixedPointType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}

	const FName VarName = FBlueprintEditorUtils::FindUniqueKismetName(
		BP, TEXT("NewField"));
	if (FBlueprintEditorUtils::AddMemberVariable(BP, VarName, FixedPointType))
	{
		const int32 VarIndex =
			FBlueprintEditorUtils::FindNewVariableIndex(BP, VarName);
		if (VarIndex != INDEX_NONE)
		{
			BP->NewVariables[VarIndex].PropertyFlags |= CPF_Edit;
			BP->NewVariables[VarIndex].PropertyFlags &=
				~CPF_DisableEditOnInstance;
		}
		CompileAndRefresh();
	}
}

void FSeinEntityComponentBlueprintEditor::OnRemoveVariable(FGuid VarGuid)
{
	UBlueprint* BP = Blueprint.Get();
	if (!BP)
	{
		return;
	}
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarGuid == VarGuid)
		{
			const FScopedTransaction Transaction(
				LOCTEXT("RemoveVariableTransaction", "Remove Component Variable"));
			FBlueprintEditorUtils::RemoveMemberVariable(BP, Var.VarName);
			CompileAndRefresh();
			return;
		}
	}
}

void FSeinEntityComponentBlueprintEditor::OnRenameVariable(
	FGuid VarGuid, const FText& NewName)
{
	UBlueprint* BP = Blueprint.Get();
	if (!BP || NewName.IsEmptyOrWhitespace())
	{
		return;
	}
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarGuid == VarGuid)
		{
			const FName NewFName(*NewName.ToString());
			if (Var.VarName == NewFName)
			{
				return;
			}
			// Same validation the My Blueprint panel applies — duplicates and
			// illegal names would otherwise reach RenameMemberVariable and
			// desync the name-keyed payload sync.
			FKismetNameValidator Validator(BP, Var.VarName);
			if (Validator.IsValid(NewFName.ToString()) != EValidatorResult::Ok)
			{
				RefreshVariablesPanel(); // snap the row back to the real name
				return;
			}
			const FScopedTransaction Transaction(
				LOCTEXT("RenameVariableTransaction", "Rename Component Variable"));
			FBlueprintEditorUtils::RenameMemberVariable(
				BP, Var.VarName, NewFName);
			CompileAndRefresh();
			return;
		}
	}
}

void FSeinEntityComponentBlueprintEditor::OnVariableTypeChanged(
	FGuid VarGuid, const FEdGraphPinType& NewType)
{
	UBlueprint* BP = Blueprint.Get();
	if (!BP)
	{
		return;
	}
	// Belt on top of the picker filter (custom filters cannot stop every
	// path, e.g. sub-type pickers).
	if (!SeinDeterminism::IsPinTypeDeterministic(NewType))
	{
		return;
	}
	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		if (Var.VarGuid == VarGuid)
		{
			if (Var.VarType == NewType)
			{
				return;
			}
			const FScopedTransaction Transaction(
				LOCTEXT("ChangeTypeTransaction", "Change Component Variable Type"));
			FBlueprintEditorUtils::ChangeMemberVariableType(
				BP, Var.VarName, NewType);
			CompileAndRefresh();
			return;
		}
	}
}

#undef LOCTEXT_NAMESPACE
