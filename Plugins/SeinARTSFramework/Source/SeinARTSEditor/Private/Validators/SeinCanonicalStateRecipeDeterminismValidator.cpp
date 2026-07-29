/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRecipeDeterminismValidator.cpp
 */

#include "Validators/SeinCanonicalStateRecipeDeterminismValidator.h"

#include "Engine/Blueprint.h"
#include "Misc/DataValidation.h"
#include "Simulation/SeinCanonicalStateRecipe.h"
#include "Validators/SeinContextFreeRecipeDeterminism.h"

USeinCanonicalStateRecipeDeterminismValidator::
	USeinCanonicalStateRecipeDeterminismValidator()
{
	bIsEnabled = true;
}

bool USeinCanonicalStateRecipeDeterminismValidator::
	CanValidateAsset_Implementation(
		const FAssetData&,
		UObject* InObject,
		FDataValidationContext&) const
{
	const UBlueprint* Blueprint = Cast<UBlueprint>(InObject);
	return Blueprint
		&& Blueprint->ParentClass
		&& Blueprint->ParentClass->IsChildOf(
			USeinCanonicalStateRecipe::StaticClass());
}

EDataValidationResult USeinCanonicalStateRecipeDeterminismValidator::
	ValidateLoadedAsset_Implementation(
		const FAssetData&,
		UObject* InAsset,
		FDataValidationContext&)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(InAsset);
	if (!Blueprint)
	{
		return EDataValidationResult::NotValidated;
	}

	TArray<FText> Diagnostics;
	if (!SeinContextFreeRecipeDeterminism::ValidateBlueprint(
		Blueprint,
		Diagnostics))
	{
		for (const FText& Diagnostic : Diagnostics)
		{
			AssetFails(InAsset, Diagnostic);
		}
		return EDataValidationResult::Invalid;
	}

	AssetPasses(InAsset);
	return EDataValidationResult::Valid;
}
