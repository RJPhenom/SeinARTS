/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDeterminismValidator.h
 * @brief   Determinism validator for movement-mode Blueprints (USeinMovement subclasses). Thin scope +
 *          messaging over the shared USeinBlueprintDeterminismValidator, which owns the graph walk
 *          (direct + macro recursion), the deterministic-call whitelist, and the RNG denylist. Opts into
 *          blocking errors via Project Settings → SeinARTS → Movement (bMovementDeterminismIsError).
 */

#pragma once

#include "CoreMinimal.h"
#include "Validators/SeinBlueprintDeterminismValidator.h"
#include "SeinMovementDeterminismValidator.generated.h"

class UBlueprint;

UCLASS()
class USeinMovementDeterminismValidator : public USeinBlueprintDeterminismValidator
{
	GENERATED_BODY()

protected:
	virtual bool IsTargetBlueprint(UBlueprint* Blueprint) const override;
	virtual FText GetAssetKindLabel() const override;
	virtual FText GetToolkitHintText() const override;
	virtual bool ShouldEscalateToError() const override;
};
