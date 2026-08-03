/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationDeterminismValidator.cpp
 */

#include "Validators/SeinFormationDeterminismValidator.h"
#include "Formations/SeinFormation.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "SeinFormationDeterminismValidator"

namespace
{
	void CollectFormationVariableSets(
		UEdGraph* Graph,
		TSet<UEdGraph*>& Visited,
		TArray<UK2Node_VariableSet*>& OutSets)
	{
		if (!Graph || Visited.Contains(Graph))
		{
			return;
		}
		Visited.Add(Graph);
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_VariableSet* Set =
					Cast<UK2Node_VariableSet>(Node))
			{
				OutSets.Add(Set);
			}
			else if (UK2Node_MacroInstance* Macro =
					Cast<UK2Node_MacroInstance>(Node))
			{
				CollectFormationVariableSets(
					Macro->GetMacroGraph(), Visited, OutSets);
			}
		}
	}
}

bool USeinFormationDeterminismValidator::IsTargetBlueprint(UBlueprint* Blueprint) const
{
	return Blueprint
		&& Blueprint->ParentClass
		&& Blueprint->ParentClass->IsChildOf(USeinFormation::StaticClass());
}

FText USeinFormationDeterminismValidator::GetAssetKindLabel() const
{
	return LOCTEXT("FormationKind", "Formation");
}

FText USeinFormationDeterminismValidator::GetToolkitHintText() const
{
	return LOCTEXT("FormationHint", "formation toolkit");
}

EDataValidationResult USeinFormationDeterminismValidator::
	ValidateLoadedAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InAsset,
		FDataValidationContext& Context)
{
	const EDataValidationResult BaseResult =
		Super::ValidateLoadedAsset_Implementation(
			InAssetData, InAsset, Context);
	UBlueprint* Blueprint = Cast<UBlueprint>(InAsset);
	if (!Blueprint || !IsTargetBlueprint(Blueprint))
	{
		return BaseResult;
	}

	TArray<UK2Node_VariableSet*> SetNodes;
	TSet<UEdGraph*> Visited;
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		CollectFormationVariableSets(Graph, Visited, SetNodes);
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		CollectFormationVariableSets(Graph, Visited, SetNodes);
	}
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		CollectFormationVariableSets(Graph, Visited, SetNodes);
	}

	bool bWritesMemberState = false;
	for (const UK2Node_VariableSet* Set : SetNodes)
	{
		if (!Set
			|| !Set->VariableReference.IsSelfContext()
			|| Set->VariableReference.IsLocalScope())
		{
			continue;
		}
		bWritesMemberState = true;
		AssetFails(
			InAsset,
			FText::Format(
				LOCTEXT("FormationMemberWrite",
					"Formation writes member variable '{0}'. Formation instances are pure configuration evaluators shared by preview and commit; keep per-order working state in function-local variables."),
				FText::FromName(Set->GetVarName())));
	}

	return bWritesMemberState
		? EDataValidationResult::Invalid
		: BaseResult;
}

#undef LOCTEXT_NAMESPACE
