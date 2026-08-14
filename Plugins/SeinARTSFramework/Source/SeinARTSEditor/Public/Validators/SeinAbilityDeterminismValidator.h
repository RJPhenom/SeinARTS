/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAbilityDeterminismValidator.h
 * @author       RJ Macklem
 * @created      14 Aug 2026
 * @latest       14 Aug 2026
 * @brief        Blocking deterministic-state validation for Ability Blueprints.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Validators/SeinBlueprintDeterminismValidator.h"
#include "SeinAbilityDeterminismValidator.generated.h"

class UBlueprint;

/** Ability graphs execute on the lockstep simulation spine, so unsafe calls or
 *  member state must fail validation rather than enter a build. */
UCLASS()
class USeinAbilityDeterminismValidator
	: public USeinBlueprintDeterminismValidator
{
	GENERATED_BODY()

protected:
	virtual bool IsTargetBlueprint(UBlueprint* Blueprint) const override;
	virtual FText GetAssetKindLabel() const override;
	virtual FText GetToolkitHintText() const override;
	virtual bool ShouldEscalateToError() const override { return true; }
};
