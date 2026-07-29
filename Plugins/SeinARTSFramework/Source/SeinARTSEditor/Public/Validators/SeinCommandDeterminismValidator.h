/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCommandDeterminismValidator.h
 * @brief   Blocking determinism validation for command policies and handlers.
 */

#pragma once

#include "CoreMinimal.h"
#include "Validators/SeinBlueprintDeterminismValidator.h"
#include "SeinCommandDeterminismValidator.generated.h"

class UBlueprint;

/** Command strategies execute on the lockstep spine, so unsafe graphs cannot cook. */
UCLASS()
class USeinCommandDeterminismValidator : public USeinBlueprintDeterminismValidator
{
	GENERATED_BODY()

protected:
	virtual bool IsTargetBlueprint(UBlueprint* Blueprint) const override;
	virtual FText GetAssetKindLabel() const override;
	virtual FText GetToolkitHintText() const override;
	virtual bool ShouldEscalateToError() const override { return true; }
};
