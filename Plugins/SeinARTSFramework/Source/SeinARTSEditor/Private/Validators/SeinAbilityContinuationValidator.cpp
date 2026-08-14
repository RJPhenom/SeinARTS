/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAbilityContinuationValidator.cpp
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       14 Aug 2026
 * @brief        Implements checkpoint-continuation validation for Ability Blueprints.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Validators/SeinAbilityContinuationValidator.h"

#include "Engine/Blueprint.h"
#include "Misc/DataValidation.h"
#include "Validators/SeinAbilityContinuationAnalysis.h"

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

	if (bInvalid)
	{
		return EDataValidationResult::Invalid;
	}
	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}
