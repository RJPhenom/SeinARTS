/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationDeterminismValidator.h
 * @brief   Determinism validator for formation Blueprints (USeinFormation subclasses). A formation runs
 *          in BOTH the destination preview AND the commit dispatch and must agree bit-for-bit — a float
 *          node in BuildFormation desyncs lockstep and splits preview from commit. Thin scope + messaging
 *          over the shared USeinBlueprintDeterminismValidator (walk + whitelist + RNG denylist). Warnings
 *          only for now (no escalation setting yet — see ShouldEscalateToError on the base).
 */

#pragma once

#include "CoreMinimal.h"
#include "Validators/SeinBlueprintDeterminismValidator.h"
#include "SeinFormationDeterminismValidator.generated.h"

class UBlueprint;

UCLASS()
class USeinFormationDeterminismValidator : public USeinBlueprintDeterminismValidator
{
	GENERATED_BODY()

protected:
	virtual bool IsTargetBlueprint(UBlueprint* Blueprint) const override;
	virtual FText GetAssetKindLabel() const override;
	virtual FText GetToolkitHintText() const override;
};
