/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * Save/Validate fallback for async compiler-frame values covered by the
 * deterministic Move To continuation contract.
 * The compiler gate and this validator both cover every Blueprint whose class
 * derives USeinAbility, including generic/imported/legacy UBlueprint assets.
 * The validator is save/Validate redundancy for the blocking compiler rule.
 * Native/external impure functions intentionally used after a restored
 * callback must declare UFUNCTION(meta=(SeinContinuationSafe)); that contract
 * promises their future-affecting mutations use canonical Sein state.
 */

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "SeinAbilityContinuationValidator.generated.h"

UCLASS()
class SEINARTSEDITOR_API USeinAbilityContinuationValidator
	: public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	USeinAbilityContinuationValidator();

	virtual bool CanValidateAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InObject,
		FDataValidationContext& InContext) const override;

	virtual EDataValidationResult ValidateLoadedAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InAsset,
		FDataValidationContext& Context) override;
};
