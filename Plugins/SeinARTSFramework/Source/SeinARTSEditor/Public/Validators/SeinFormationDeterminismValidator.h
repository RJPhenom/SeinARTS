/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationDeterminismValidator.h
 * @brief   Determinism validator for formation Blueprints (USeinFormation subclasses). A formation runs
 *          in BOTH the destination preview AND the commit dispatch and must agree bit-for-bit — a float
 *          node in BuildFormation desyncs lockstep and splits preview from commit. Thin scope + messaging
 *          over the shared USeinBlueprintDeterminismValidator (walk + whitelist + RNG denylist).
 *          Findings and writes to formation member state are blocking errors.
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

public:
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(
		const FAssetData& InAssetData,
		UObject* InAsset,
		FDataValidationContext& Context) override;

protected:
	virtual bool IsTargetBlueprint(UBlueprint* Blueprint) const override;
	virtual FText GetAssetKindLabel() const override;
	virtual FText GetToolkitHintText() const override;
	virtual bool ShouldEscalateToError() const override { return true; }
};
