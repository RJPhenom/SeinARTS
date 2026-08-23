/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatTargetIndex.h
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Declares the derived spatial index used to prefilter target
 *               acquisition candidates without changing query semantics.
 *               Membership is every live entity; which of them are targets is
 *               the query's (and the scorer's) business.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Types/Vector.h"

class USeinWorldSubsystem;

/** Process-local acceleration cache. Entries and revision sentinels are
 *  derived from canonical world state and never serialized or hashed. */
class FSeinCombatTargetIndex
{
public:
	/** Append a canonical handle-ordered spatial prefilter. Returns false when
	 *  the query span is large enough that the caller should use a full sweep. */
	bool QueryRadius(
		const USeinWorldSubsystem& World,
		const FFixedVector& Origin,
		FFixedPoint Radius,
		FSeinEntityHandle Exclude,
		TArray<FSeinEntityHandle>& OutHandles) const;

	/** Drop all derived entries and revision sentinels. */
	void Invalidate();

private:
	struct FCellEntry
	{
		int64 CellKey = 0;
		FSeinEntityHandle Handle;
	};

	static constexpr int32 CellSizeUnits = 512;

	static int64 MakeCellKey(int32 CellX, int32 CellY);
	static int32 ToCell(FFixedPoint WorldCoord);
	void RebuildIfStale(const USeinWorldSubsystem& World) const;

	mutable TArray<FCellEntry> Entries;
	mutable uint64 EntityTopologyRevision = 0;
	mutable uint64 EntityMutationRevision = 0;
	mutable bool bValid = false;
};
