/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDeterminismValidator.cpp
 */

#include "Validators/SeinMovementDeterminismValidator.h"
#include "Util/SeinMovementTuningExport.h"  // IsMovementModeBlueprint

#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/DataValidation.h"
#include "UObject/Class.h"

#define LOCTEXT_NAMESPACE "SeinMovementDeterminismValidator"

namespace
{
	const FName SeinDeterministicMeta(TEXT("SeinDeterministic"));

	/** A call is allowed if the target function (or its owning class) is tagged
	 *  SeinDeterministic. Unresolved targets are not flagged (can't judge). */
	bool IsCallWhitelisted(const UFunction* Func)
	{
		if (!Func) return true;
		if (Func->HasMetaData(SeinDeterministicMeta)) return true;
		if (const UClass* Owner = Func->GetOwnerClass())
		{
			return Owner->HasMetaData(SeinDeterministicMeta);
		}
		return false;
	}
}

USeinMovementDeterminismValidator::USeinMovementDeterminismValidator()
{
	bIsEnabled = true;
}

bool USeinMovementDeterminismValidator::CanValidateAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InObject, FDataValidationContext& /*InContext*/) const
{
	return SeinMovementTuning::IsMovementModeBlueprint(Cast<UBlueprint>(InObject));
}

EDataValidationResult USeinMovementDeterminismValidator::ValidateLoadedAsset_Implementation(const FAssetData& /*InAssetData*/, UObject* InAsset, FDataValidationContext& /*Context*/)
{
	UBlueprint* BP = Cast<UBlueprint>(InAsset);
	if (!BP)
	{
		return EDataValidationResult::NotValidated;
	}

	TArray<UK2Node_CallFunction*> CallNodes;
	FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallFunction>(BP, CallNodes);

	for (UK2Node_CallFunction* Node : CallNodes)
	{
		if (!Node) continue;
		const UFunction* Func = Node->GetTargetFunction();
		if (IsCallWhitelisted(Func)) continue;

		const FString FuncName  = Func ? Func->GetName() : TEXT("<unresolved>");
		const UClass* Owner     = Func ? Func->GetOwnerClass() : nullptr;
		const FString OwnerName = Owner ? Owner->GetName() : TEXT("<unknown>");

		AssetWarning(InAsset, FText::Format(
			LOCTEXT("NonDeterministicCall",
				"Movement mode calls non-deterministic '{0}::{1}' — this can desync lockstep. Use the "
				"SeinARTS fixed-point math / Sein Mover Handle nodes, or confirm the call is deterministic."),
			FText::FromString(OwnerName), FText::FromString(FuncName)));
	}

	// V1 reports warnings only (never blocks the asset). AssetPasses marks it as checked.
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}

#undef LOCTEXT_NAMESPACE
