/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinTargetQueryService.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Deterministic on-demand target acquisition.
 *
 *          A pure query: sweep alive vitals-bearing entities in canonical
 *          order, gate mechanically (range, arc, tags, fog LoS through the
 *          bound resolver), ask the scorer policy for validity + score, and
 *          return the best candidates. No retained state, no always-on
 *          engagement loop — abilities and effects call this when THEY decide
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

private:
	FSeinTargetQueryService() = delete;
};
