/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatPolicyDeterminismValidator.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Declares blocking deterministic and statelessness validation
 *               for Combat policy Blueprints evaluated on shared class
 *               default objects.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Validators/SeinBlueprintDeterminismValidator.h"
#include "SeinCombatPolicyDeterminismValidator.generated.h"

class UBlueprint;

UCLASS()
class USeinCombatPolicyDeterminismValidator
	: public USeinBlueprintDeterminismValidator
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
