/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTargetScorer.cpp
 * @brief   Neutral scorer defaults: exclude same-owner, nearest wins.
 */

#include "Combat/SeinTargetScorer.h"
#include "Simulation/SeinWorldSubsystem.h"

bool USeinTargetScorer::IsValidTarget_Implementation(
	const USeinWorldSubsystem* World,
	const FSeinTargetQuery& Query,
	FSeinEntityHandle Candidate) const
{
	// Hostility is policy, and the neutral default is deliberately minimal:
	// anything not owned by the instigator's player is fair game. Alliance-
	// aware games override this (typically consulting a pair-capability tag).
	if (!World || !Query.Instigator.IsValid())
	{
		return true;
	}
	return World->GetEntityOwner(Candidate)
		!= World->GetEntityOwner(Query.Instigator);
}

FFixedPoint USeinTargetScorer::ScoreTarget_Implementation(
	const USeinWorldSubsystem* World,
	const FSeinTargetQuery& Query,
	const FSeinTargetCandidate& Candidate) const
{
	// Nearer is better. Range minus distance keeps scores positive and
	// bounded by the query's own range.
	return Query.Range - Candidate.Distance;
}
