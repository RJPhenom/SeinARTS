/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinTargetQueryService.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Deterministic on-demand target acquisition and per-target
 *               eligibility checks.
 *
 *          A pure query over canonical state: a derived Combat-owned spatial
 *          index prefilters live entities by position, then the service gates
 *          mechanically (alive, required component, range, arc, tags, fog LoS
 *          through the bound resolver), asks the scorer policy for validity +
 *          score, and returns the best candidates. Check Target runs the SAME
 *          gate chain against one chosen entity and reports the first failing
 *          gate, so "find something to shoot" and "may I shoot this" can never
 *          disagree on the GATES (the only differences are Find Targets'
 *          MaxResults cut-off and the instigator's own exclusion). No always-on
 *          engagement loop — abilities and effects call these when THEY decide
 *          to look for trouble.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatTypes.h"

class USeinWorldSubsystem;

class SEINARTSCOMBAT_API FSeinTargetQueryService
{
public:
	/** Run one acquisition query. Results are best-scored first, exact ties
	 *  broken by canonical entity order, bounded by Query.MaxResults. */
	static void FindTargets(
		const USeinWorldSubsystem& World,
		const FSeinTargetQuery& Query,
		TArray<FSeinTargetCandidate>& OutCandidates);

	/** Evaluate ONE entity against the query's gates (the exact chain
	 *  FindTargets applies) and report the first failing gate. MaxResults is
	 *  ignored. When Eligible, OutCandidate carries the distance and score. */
	static ESeinTargetCheckResult CheckTarget(
		const USeinWorldSubsystem& World,
		const FSeinTargetQuery& Query,
		FSeinEntityHandle Target,
		FSeinTargetCandidate& OutCandidate);

private:
	FSeinTargetQueryService() = delete;
};
