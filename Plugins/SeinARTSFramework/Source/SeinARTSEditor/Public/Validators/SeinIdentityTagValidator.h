/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinIdentityTagValidator.h
 * @author       RJ Macklem
 * @created      7 Sep 2026
 * @latest       7 Sep 2026
 * @brief        Validates identity tags independently of generation and asset naming.
 * @disclaimer   Generated with assistance from an AI language model.
 */
#pragma once
#include "EditorValidatorBase.h"
#include "SeinIdentityTagValidator.generated.h"

UCLASS()
class USeinIdentityTagValidator : public UEditorValidatorBase
{
	GENERATED_BODY()
public:
	USeinIdentityTagValidator();
	virtual bool CanValidateAsset_Implementation(const FAssetData& Asset, UObject* Object, FDataValidationContext& Context) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& Asset, UObject* Object, FDataValidationContext& Context) override;
};
