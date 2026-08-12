#include "Validators/SeinAbilityContinuationValidator.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DataValidation.h"
#include "Validators/SeinAbilityContinuationAnalysis.h"

namespace
{
	const FName SeinPresentationOnlyMeta(TEXT("SeinPresentationOnly"));

	void CollectMacroCallNodes(
		UEdGraph* Graph,
		TSet<UEdGraph*>& Visited,
		TArray<UK2Node_CallFunction*>& OutCalls)
	{
		if (!Graph || Visited.Contains(Graph))
		{
			return;
		}
		Visited.Add(Graph);
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_CallFunction* Call =
				Cast<UK2Node_CallFunction>(Node))
			{
				OutCalls.Add(Call);
			}
			else if (UK2Node_MacroInstance* Macro =
				Cast<UK2Node_MacroInstance>(Node))
			{
				CollectMacroCallNodes(
					Macro->GetMacroGraph(), Visited, OutCalls);
			}
		}
	}
}

USeinAbilityContinuationValidator::USeinAbilityContinuationValidator()
{
	bIsEnabled = true;
}

bool USeinAbilityContinuationValidator::CanValidateAsset_Implementation(
	const FAssetData&,
	UObject* InObject,
	FDataValidationContext&) const
{
	return FSeinAbilityContinuationAnalysis::IsAbilityBlueprint(
		Cast<UBlueprint>(InObject));
}

EDataValidationResult
USeinAbilityContinuationValidator::ValidateLoadedAsset_Implementation(
	const FAssetData&,
	UObject* InAsset,
	FDataValidationContext&)
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(InAsset);
	if (!FSeinAbilityContinuationAnalysis::IsAbilityBlueprint(Blueprint))
	{
		return EDataValidationResult::NotValidated;
	}

	TArray<FSeinAbilityContinuationFinding> Findings;
	FSeinAbilityContinuationAnalysis::Analyze(*Blueprint, Findings);
	bool bInvalid = false;
	for (const FSeinAbilityContinuationFinding& Finding : Findings)
	{
		AssetFails(InAsset, FText::FromString(Finding.ToDiagnostic()));
		bInvalid = true;
	}

	TArray<UK2Node_CallFunction*> CallNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallFunction>(
		Blueprint, CallNodes);
	TArray<UK2Node_MacroInstance*> MacroNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_MacroInstance>(
		Blueprint, MacroNodes);
	TSet<UEdGraph*> Visited;
	for (UK2Node_MacroInstance* Macro : MacroNodes)
	{
		if (Macro)
		{
			CollectMacroCallNodes(
				Macro->GetMacroGraph(), Visited, CallNodes);
		}
	}
	for (UK2Node_CallFunction* Node : CallNodes)
	{
		const UFunction* Function = Node
			? Node->GetTargetFunction()
			: nullptr;
		if (!Function
			|| !Function->HasMetaData(SeinPresentationOnlyMeta))
		{
			continue;
		}
		AssetFails(InAsset, FText::Format(
			NSLOCTEXT(
				"SeinAbilityContinuationValidator",
				"PresentationOnlyCall",
				"Ability calls presentation-only function '{0}::{1}'. Transient render state cannot drive deterministic simulation."),
			FText::FromString(
				Function->GetOwnerClass()
					? Function->GetOwnerClass()->GetName()
					: TEXT("<unknown>")),
			FText::FromName(Function->GetFName())));
		bInvalid = true;
	}

	if (bInvalid)
	{
		return EDataValidationResult::Invalid;
	}
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}
