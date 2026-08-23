/**
 * SeinARTS Test Suite - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatTestTypes.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Implements native Combat policy doubles used by framework tests.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "TestTypes/SeinCombatTestTypes.h"

#include "Simulation/SeinWorldSubsystem.h"

bool USeinFarthestTargetScorerTestDouble::IsValidTarget_Implementation(
	const USeinWorldSubsystem*,
	const FSeinTargetQuery&,
	FSeinEntityHandle) const
{
	return true;
}

FFixedPoint USeinFarthestTargetScorerTestDouble::ScoreTarget_Implementation(
	const USeinWorldSubsystem*,
	const FSeinTargetQuery&,
	const FSeinTargetCandidate& Candidate) const
{
	return Candidate.Distance;
}

bool USeinLivingVitalsTargetScorerTestDouble::IsValidTarget_Implementation(
	const USeinWorldSubsystem* World,
	const FSeinTargetQuery& Query,
	FSeinEntityHandle Candidate) const
{
	if (!Super::IsValidTarget_Implementation(World, Query, Candidate))
	{
		return false;
	}
	const FSeinTestVitalsComponent* Vitals = World
		? World->GetComponent<FSeinTestVitalsComponent>(Candidate)
		: nullptr;
	return Vitals && Vitals->Health > FFixedPoint::Zero;
}
