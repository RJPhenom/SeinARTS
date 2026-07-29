/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCanonicalStateRecipeDeterminismValidator.h
 * @brief   Blocking Data Validation gate for context-free recipe Blueprints.
 */

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "SeinCanonicalStateRecipeDeterminismValidator.generated.h"

/** Runs on save/Validate Assets; PIE/cook invoke the same analysis directly. */
UCLASS()
class USeinCanonicalStateRecipeDeterminismValidator
	: public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	USeinCanonicalStateRecipeDeterminismValidator();

	virtual bool CanValidateAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InObject,
		FDataValidationContext& InContext) const override;

	virtual EDataValidationResult ValidateLoadedAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InAsset,
		FDataValidationContext& Context) override;
};
