/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDeterminismValidator.h
 * @brief   Data-validation pass for movement-mode Blueprints (USeinMovement subclasses).
 *          Walks the BP's graphs and WARNS on any function call whose target isn't
 *          whitelisted as deterministic — neither the function nor its owning class carries
 *          the `SeinDeterministic` meta. This is the guard for the Tier-2 power route, where
 *          a non-deterministic node in a sim-tick graph would desync lockstep.
 *
 *          Auto-gathered at editor start (UEditorValidatorBase); runs on save and on
 *          "Validate Assets". Warnings (not hard errors) for V1 — escalatable to errors
 *          (AssetFails) once the whitelist coverage is confirmed in practice.
 */

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "SeinMovementDeterminismValidator.generated.h"

UCLASS()
class USeinMovementDeterminismValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	USeinMovementDeterminismValidator();

	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
