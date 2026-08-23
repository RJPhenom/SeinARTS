/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatBPFL.cpp
 * @brief   Read-side combat toolkit query implementations.
 */

#include "Lib/SeinCombatBPFL.h"
#include "Combat/SeinTargetQueryService.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	const USeinWorldSubsystem* GetSim(const UObject* WorldContextObject)
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(
				WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	}
}

TArray<FSeinTargetCandidate> USeinCombatBPFL::SeinFindTargets(
	const UObject* WorldContextObject, const FSeinTargetQuery& Query)
{
	TArray<FSeinTargetCandidate> Candidates;
	if (const USeinWorldSubsystem* Sim = GetSim(WorldContextObject))
	{
		FSeinTargetQueryService::FindTargets(*Sim, Query, Candidates);
	}
	return Candidates;
}

ESeinTargetCheckResult USeinCombatBPFL::SeinCheckTarget(
	const UObject* WorldContextObject,
	const FSeinTargetQuery& Query,
	FSeinEntityHandle Target,
	FSeinTargetCandidate& Candidate)
{
	Candidate = FSeinTargetCandidate();
	const USeinWorldSubsystem* Sim = GetSim(WorldContextObject);
	if (!Sim)
	{
		return ESeinTargetCheckResult::InvalidTarget;
	}
	return FSeinTargetQueryService::CheckTarget(
		*Sim, Query, Target, Candidate);
}
