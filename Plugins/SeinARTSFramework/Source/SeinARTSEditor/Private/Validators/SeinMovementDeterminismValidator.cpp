/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDeterminismValidator.cpp
 */

#include "Validators/SeinMovementDeterminismValidator.h"
#include "Util/SeinMovementTuningExport.h"  // IsMovementModeBlueprint

#define LOCTEXT_NAMESPACE "SeinMovementDeterminismValidator"

bool USeinMovementDeterminismValidator::IsTargetBlueprint(UBlueprint* Blueprint) const
{
	return SeinMovementTuning::IsMovementModeBlueprint(Blueprint);
}

FText USeinMovementDeterminismValidator::GetAssetKindLabel() const
{
	return LOCTEXT("MovementKind", "Movement mode");
}

FText USeinMovementDeterminismValidator::GetToolkitHintText() const
{
	return LOCTEXT("MovementHint", "Sein Mover Handle");
}

bool USeinMovementDeterminismValidator::ShouldEscalateToError() const
{
	// Movement-mode non-determinism is ALWAYS a blocking error, never a warning: a non-deterministic
	// mover breaks lockstep, so it must block Data Validation and cook rather than be dismissible.
	return true;
}

#undef LOCTEXT_NAMESPACE
