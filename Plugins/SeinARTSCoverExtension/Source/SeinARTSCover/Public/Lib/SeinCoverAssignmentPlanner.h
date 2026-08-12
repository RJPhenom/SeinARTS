/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverAssignmentPlanner.h
 * @brief   Shared deterministic selection-wide cover-slot assignment.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/SeinCoverTypes.h"

class USeinWorldSubsystem;

/** One pure-plan assignment from a member destination to a queried slot. */
struct SEINARTSCOVER_API FSeinCoverSlotAssignment
{
	int32 MemberIndex = INDEX_NONE;
	int32 SlotCandidateIndex = INDEX_NONE;
	FFixedPoint DistanceSquared = FFixedPoint::Zero;
	bool bPreferredSide = false;
};

/**
 * Pure cover assignment artifact. It does not reserve slots or mutate sim
 * state, so preview and command resolution can build the same result.
 */
struct SEINARTSCOVER_API FSeinCoverAssignmentPlan
{
	TArray<FSeinCoverSlotAssignment> Assignments;
	int32 EligibleMemberCount = 0;
	int32 PreferredAssignmentCount = 0;

	int32 Num() const { return Assignments.Num(); }
	int32 WrongSideAssignmentCount() const
	{
		return Assignments.Num() - PreferredAssignmentCount;
	}

	/** Apply this exact artifact to the matching member-position array. */
	void Apply(
		TArray<FFixedVector>& InOutPositions,
		const TArray<FSeinCoverSlotCandidate>& Slots) const;
};

/** Shared ordinary-unit and squad cover assignment implementation. */
class SEINARTSCOVER_API FSeinCoverAssignmentPlanner
{
public:
	/**
	 * Solve one deterministic bipartite assignment. The objective is
	 * lexicographic:
	 *   1. maximum assignment cardinality;
	 *   2. minimum wrong-side assignment count;
	 *   3. minimum total fixed-point squared distance.
	 *
	 * MemberIndices and slot candidate order must already be stable. Equal
	 * objective ties preserve those orders. Invalid member indices and repeated
	 * member indices after their first occurrence are ignored, as are invalid or
	 * duplicate preferred indices. Members can use only slots within SnapRadius.
	 * Inputs whose squared distances or aggregate objective exceed the exact
	 * signed 64-bit representation fail closed with an empty assignment.
	 * Remaining exact-objective ties are resolved deterministically from stable
	 * row and column traversal; they do not promise the lowest-index slot for
	 * each member when an equally optimal augmenting reroute exists.
	 */
	static FSeinCoverAssignmentPlan Solve(
		const TArray<FFixedVector>& DesiredPositions,
		const TArray<int32>& MemberIndices,
		const TArray<FSeinCoverSlotCandidate>& Slots,
		TConstArrayView<int32> PreferredSlotIndices,
		FFixedPoint SnapRadius);

	/**
	 * Build the shipped cover plan: filter members by UsesCover, partition
	 * queried slots by cursor side, then run Solve. This is the single planning
	 * path used by ordinary and squad resolvers.
	 */
	static FSeinCoverAssignmentPlan PlanForMembers(
		USeinWorldSubsystem* World,
		const TArray<FSeinEntityHandle>& Members,
		const TArray<FFixedVector>& DesiredPositions,
		const TArray<FSeinCoverSlotCandidate>& Slots,
		FFixedVector TargetLocation,
		FFixedPoint SnapRadius);
};
