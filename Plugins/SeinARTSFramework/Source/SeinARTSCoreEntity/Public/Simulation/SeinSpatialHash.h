/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSpatialHash.h
 * @brief   Deterministic 2D bucket grid keyed by integer cell coords. Used by
 *          the avoidance pipeline (penetration resolution + per-movement
 *          anticipation) to answer "give me handles within radius R of point
 *          P" in O(K) where K is the bucket fan-out.
 *
 *          Design notes:
 *          - Pure C++; no UObject. Lives as a member of USeinWorldSubsystem.
 *          - Cell key = (cellX << 32) | (cellY & 0xFFFFFFFF). Sign bias is
 *            handled by reinterpret-as-uint32 — both axes accept negative
 *            world coords without aliasing.
 *          - Buckets store FSeinEntityHandle. Insertion order within a
 *            bucket reflects the entity-pool insertion order (callers walk
 *            the pool in handle-index order before inserting).
 *          - Query results sorted by handle index ascending before return.
 *            This is the only step that costs anything per query, but it's
 *            essential to defeat TMap bucket-iteration nondeterminism.
 *          - Full clear+rebuild each tick (MVP); incremental updates are a
 *            later optimization (Phase E in the avoidance plan).
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

class SEINARTSCOREENTITY_API FSeinSpatialHash
{
public:
	FSeinSpatialHash();

	/** Configure cell size and the world-space origin used for cell coord
	 *  conversion. Origin lets the grid have well-defined cell numbers even
	 *  when world coords are large (e.g. levels offset from 0,0). Idempotent
	 *  to call again — clears existing buckets and re-arms with new params. */
	void Initialize(FFixedPoint InCellSize, FFixedVector InOrigin);

	/** Drop all entries. Buckets retain capacity (TArray::Reset semantics). */
	void Clear();

	/** Insert a handle at the given world position. Multiple inserts of the
	 *  same handle are allowed but should be avoided — caller (the rebuild
	 *  system) iterates each entity once per tick. */
	void Insert(FSeinEntityHandle Handle, const FFixedVector& Pos);

	/** Return all handles within `Radius` of `QueryPos`. Results are sorted
	 *  by handle index ascending. `Exclude`, if valid, is filtered out (used
	 *  for self-exclusion in avoidance queries). Output is appended — caller
	 *  should `Reset()` if they want a fresh array. */
	void QueryRadius(
		const FFixedVector& QueryPos,
		FFixedPoint Radius,
		TArray<FSeinEntityHandle>& Out,
		FSeinEntityHandle Exclude = FSeinEntityHandle()) const;

	/** Diagnostic: number of non-empty buckets. */
	int32 NumBuckets() const { return Buckets.Num(); }

	/** Configured cell size. */
	FFixedPoint GetCellSize() const { return CellSize; }

private:
	/** Pack a (cellX, cellY) into the int64 bucket key. uint32 reinterpret
	 *  handles negatives without aliasing — two distinct (X, Y) pairs map to
	 *  distinct keys regardless of sign. */
	static FORCEINLINE int64 MakeKey(int32 CellX, int32 CellY)
	{
		const uint32 X = static_cast<uint32>(CellX);
		const uint32 Y = static_cast<uint32>(CellY);
		return (static_cast<int64>(X) << 32) | static_cast<int64>(Y);
	}

	/** Convert a world coord to a cell index along one axis. Floor division
	 *  via integer cast on the raw fp value — deterministic on all platforms. */
	int32 ToCell(FFixedPoint WorldCoord, FFixedPoint OriginCoord) const;

	TMap<int64, TArray<FSeinEntityHandle>> Buckets;
	FFixedPoint CellSize;
	FFixedVector Origin;
};
