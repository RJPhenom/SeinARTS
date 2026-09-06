/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinSelectionPolicy.h
 * @author       RJ Macklem
 * @created      5 Sep 2026
 * @latest       5 Sep 2026
 * @brief        Shared admission rules for local player selections.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "Core/SeinPlayerID.h"

class ASeinActor;
class UWorld;

namespace SeinSelectionPolicy
{
	/** Resolve a live actor to its squad when applicable. Missing or stale squad
	 *  links fail closed. Does not apply selection policy, so toggling may remove
	 *  an already-selected actor whose eligibility has just changed. */
	SEINARTSFRAMEWORK_API ASeinActor* ResolveActor(UWorld& World, ASeinActor* Actor);

	/** Resolve and check individual eligibility, without constructing a group. */
	SEINARTSFRAMEWORK_API ASeinActor* ResolveEligibleActor(UWorld& World, FSeinPlayerID Player,
		ASeinActor* Actor, bool bDrag = false);

	/** Normalize a selection through one rule set: live owned visible actors,
	 *  runtime selectability, nonempty extents, squad resolution, and grouping policy.
	 *  Current is retained in order where still eligible; new Candidates use priority
	 *  then full handles. Drag exclusion applies only to acquiring new candidates.
	 *  Pass an empty Current for replacement, or empty Candidates to revalidate. */
	SEINARTSFRAMEWORK_API TArray<ASeinActor*> Resolve(UWorld& World, FSeinPlayerID Player,
		const TArray<ASeinActor*>& Current, const TArray<ASeinActor*>& Candidates, bool bDrag = false);
}
