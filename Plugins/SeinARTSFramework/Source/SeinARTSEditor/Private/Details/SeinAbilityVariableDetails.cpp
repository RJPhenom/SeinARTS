/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinAbilityVariableDetails.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Stores per-variable activation exposure in Blueprint metadata.
 *
 * @disclaimer   Generated with assistance from an AI language model.
 */
#include "Details/SeinAbilityVariableDetails.h"
#include "Abilities/SeinAbility.h"
#include "BlueprintEditor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SMyBlueprint.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/UnrealType.h"

void FSeinAbilityVariableDetails::CustomizeDetails(IDetailLayoutBuilder& Builder)
{
	TSharedPtr<FBlueprintEditor> BP = StaticCastSharedPtr<FBlueprintEditor>(Editor.Pin());
	UBlueprint* Blueprint = BP ? BP->GetBlueprintObj() : nullptr;
	if (!Blueprint || !Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(USeinAbility::StaticClass())) return;
	FProperty* Property = nullptr;
	if (BP->GetMyBlueprintWidget())
		if (auto* Selection = BP->GetMyBlueprintWidget()->SelectionAsBlueprintVariable()) Property = Selection->GetProperty();
	if (!Property)
		for (UObject* Selected : BP->GetSelectedNodes())
			if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Selected)) Property = Variable->GetPropertyForVariable();
	if (!Property) return;
	const FName Name = Property->GetFName();
	// Inherited/native declarations are edited at their source.
	if (!Blueprint->NewVariables.ContainsByPredicate([Name](const FBPVariableDescription& Variable) { return Variable.VarName == Name; })) return;
	const FText Label = NSLOCTEXT("SeinAbilityInputs", "Expose", "Expose on Activate");
	TWeakObjectPtr<UBlueprint> Weak = Blueprint;
	Builder.EditCategory(TEXT("Variable")).AddCustomRow(Label)
	.NameContent()[SNew(STextBlock).Text(Label).Font(IDetailLayoutBuilder::GetDetailFont())]
	.ValueContent()[SNew(SCheckBox)
		.ToolTipText(NSLOCTEXT("SeinAbilityInputs", "ExposeTooltip", "Adds this variable as an activation input. Each command captures its own value and assigns it before On Activate. Unspecified inputs use their class defaults."))
		.IsChecked_Lambda([Weak, Name]()
		{
			UBlueprint* B = Weak.Get();
			if (!B) return ECheckBoxState::Unchecked;
			const FBPVariableDescription* Variable = B->NewVariables.FindByPredicate([Name](const FBPVariableDescription& V) { return V.VarName == Name; });
			return Variable && Variable->HasMetaData(TEXT("SeinExposeOnActivate")) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([Weak, Name](ECheckBoxState State)
		{
			UBlueprint* B = Weak.Get();
			if (!B) return;
			const FScopedTransaction Transaction(NSLOCTEXT("SeinAbilityInputs", "ChangeExposure", "Change activation input exposure"));
			B->Modify();
			if (State == ECheckBoxState::Checked)
				FBlueprintEditorUtils::SetBlueprintVariableMetaData(B, Name, nullptr, TEXT("SeinExposeOnActivate"), TEXT("true"));
			else FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(B, Name, nullptr, TEXT("SeinExposeOnActivate"));
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(B);
		})];
}
