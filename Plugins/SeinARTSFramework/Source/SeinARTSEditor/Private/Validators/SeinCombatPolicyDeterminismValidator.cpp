/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatPolicyDeterminismValidator.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Implements blocking deterministic and statelessness validation
 *               for Combat policy Blueprints evaluated on shared class
 *               default objects.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Validators/SeinCombatPolicyDeterminismValidator.h"

#include "Combat/SeinDamageFormula.h"
#include "Combat/SeinTargetScorer.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_VariableSet.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "SeinCombatPolicyDeterminismValidator"

namespace
{
	void CollectCombatPolicyVariableSets(
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
			if (UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node))
			{
				OutSets.Add(Set);
			}
			else if (UK2Node_MacroInstance* Macro =
					Cast<UK2Node_MacroInstance>(Node))
			{
				CollectCombatPolicyVariableSets(
					Macro->GetMacroGraph(), Visited, OutSets);
			}
		}
	}
}

bool USeinCombatPolicyDeterminismValidator::IsTargetBlueprint(
	UBlueprint* Blueprint) const
{
	if (!Blueprint || !Blueprint->ParentClass)
	{
		return false;
	}
	return Blueprint->ParentClass->IsChildOf(USeinDamageFormula::StaticClass())
		|| Blueprint->ParentClass->IsChildOf(USeinTargetScorer::StaticClass());
}

FText USeinCombatPolicyDeterminismValidator::GetAssetKindLabel() const
{
	return LOCTEXT("CombatPolicyKind", "Combat policy");
}

FText USeinCombatPolicyDeterminismValidator::GetToolkitHintText() const
{
	return LOCTEXT("CombatPolicyHint", "combat and fixed-point toolkit");
}

EDataValidationResult USeinCombatPolicyDeterminismValidator::
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
		CollectCombatPolicyVariableSets(Graph, Visited, SetNodes);
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		CollectCombatPolicyVariableSets(Graph, Visited, SetNodes);
	}
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		CollectCombatPolicyVariableSets(Graph, Visited, SetNodes);
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
				LOCTEXT("CombatPolicyMemberWrite",
					"Combat policy writes member variable '{0}'. Damage formulas and target scorers execute on shared class default objects; keep evaluations side-effect free and working state function-local."),
				FText::FromName(Set->GetVarName())));
	}

	return bWritesMemberState
		? EDataValidationResult::Invalid
		: BaseResult;
}

#undef LOCTEXT_NAMESPACE
