/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBalanceProfileValidator.h
 * @brief   Editor validator for USeinBalanceProfile (Balance Data). Flags configuration that would
 *          make Gather a silent no-op or surprise the designer: no Included Roots, a scope that
 *          matches no classes, an empty Tracked Components slot, or a malformed Output Directory.
 *          Warnings only — never blocks save. Auto-gathered by the Data Validation system (a concrete
 *          UEditorValidatorBase); runs on save and on "Validate Assets". No module registration.
 */

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "SeinBalanceProfileValidator.generated.h"

UCLASS()
class USeinBalanceProfileValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	USeinBalanceProfileValidator();

	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
