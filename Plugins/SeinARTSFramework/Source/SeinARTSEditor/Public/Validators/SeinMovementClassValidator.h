/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementClassValidator.h
 * @brief   Editor validator for unit Blueprints (ASeinActor subclasses): checks that each
 *          FSeinMovementPayload's MovementClass actually holds up at edit time.
 *
 *          The class picker is already filtered to USeinMovement subclasses, but a stored soft class
 *          path can still go stale (renamed/deleted mode, stripped Movement+ plugin) or point at an
 *          abstract base. At runtime an unresolvable/abstract MovementClass SILENTLY falls back to
 *          USeinBasicMovement — so a unit that should drive wheeled just moves like basic infantry,
 *          with no warning. This validator surfaces that at save / validate time. Warnings only.
 */

#pragma once

#include "CoreMinimal.h"
#include "EditorValidatorBase.h"
#include "SeinMovementClassValidator.generated.h"

UCLASS()
class USeinMovementClassValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

public:
	USeinMovementClassValidator();

	virtual bool CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InObject, FDataValidationContext& InContext) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset, FDataValidationContext& Context) override;
};
