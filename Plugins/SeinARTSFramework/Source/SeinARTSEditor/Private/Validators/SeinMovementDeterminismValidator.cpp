/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementDeterminismValidator.cpp
 */

#include "Validators/SeinMovementDeterminismValidator.h"
#include "Util/SeinMovementTuningExport.h"  // IsMovementModeBlueprint
#include "Settings/PluginSettings.h"        // USeinARTSCoreSettings

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
	// Opt-in (Project Settings -> SeinARTS -> Movement): report findings as blocking errors instead of
	// warnings, so a team can enforce lockstep-safety once its movement-mode graphs are clean.
	return GetDefault<USeinARTSCoreSettings>()->bMovementDeterminismIsError;
}

#undef LOCTEXT_NAMESPACE
