/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionSpatialHash.h
 * @brief   Deterministic two-tier 2D bucket grid — the broadphase for the
 *          COLLISION layer (extent-vs-extent). Named explicitly for collision
 *          so a build error points here, not at some generic "SpatialHash":
 *          navigation owns a separate (A*-grid) structure entirely.
 *
 *          TWO TIERS, leveraging the per-collider Static/Movable mobility:
 *            - Static tier  : persistent. Rebuilt only when the static collider
 *                             set changes (a Static collider spawned/died →
 *                             MarkStaticDirty). Walls/buildings cost nothing per
 *                             tick. Backed by a TMap bucket grid (rare rebuild,
 *                             not the per-tick cost).
 *            - Dynamic tier : rebuilt every tick from Movable AND Stationary
 *                             colliders (their positions can change). Backed by a
 *                             SORTED FLAT ARRAY of (CellKey, Handle) entries
 *                             (a "sort grid"), built once per tick via BuildDynamic
 *                             so the per-collider cell computation can parallelize
 *                             and the rebuild pays no per-cell TMap hashing/alloc.
 *          A query unions both tiers. The collision resolver iterates only
 *          Movable colliders as "self" and finds neighbours (Static + Movable)
 *          via QueryRadius, so static geometry is never iterated as a mover and
 *          Static↔Static pairs never arise.
 *
 *          Determinism: same int64 cell key + sign-safe ToCell + sort-by-handle
 *          as the rest of the sim. The dynamic tier's parallel gather writes
 *          disjoint per-collider entries and a final canonicalizing sort by
 *          (CellKey, Index, Generation) makes the flat array independent of the
 *          gather/thread order. Query results are sorted by handle index
 *          ascending to defeat any bucket/run-iteration nondeterminism. An entity
 *          lives in exactly one tier, so a query never returns duplicates across
 *          tiers (within a tier, a footprint-stamped collider is deduped).
 *
 *          Pure C++; no UObject. Lives as a member of USeinWorldSubsystem.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"

class SEINARTSCOREENTITY_API FSeinCollisionSpatialHash
{
public:
	FSeinCollisionSpatialHash();

	/** One collider's input to the batched dynamic rebuild (BuildDynamic). The
	 *  build gathers these serially (cheap per-collider filtering) then fans the
	 *  footprint cell-stamp across worker threads. */
	struct FDynamicColliderInput
	{
		FSeinEntityHandle Handle;
		FFixedVector      Pos;
		FFixedPoint       BoundingRadius;
	};

	/** Configure cell size + world-space origin for cell-coord conversion.
	 *  Idempotent — clears both tiers and re-arms. Marks the static tier dirty
	 *  so it rebuilds on the next broadphase pass. */
	void Initialize(FFixedPoint InCellSize, FFixedVector InOrigin);

	// ---- Static tier (persistent; rebuilt only when dirty) ----

	/** Flag the static tier for rebuild. Cheap (a bool). The world subsystem
	 *  calls this whenever any entity spawns/dies; the broadphase system
	 *  rebuilds the static tier on the next pass if set. */
	void MarkStaticDirty() { bStaticDirty = true; }
	bool IsStaticDirty() const { return bStaticDirty; }

	/** Static-rebuild prologue: drop all static entries. */
	void ClearStatic();
	/** Insert a Static collider, stamping EVERY cell its footprint AABB
	 *  (Pos ± BoundingRadius) covers — so a query near any part of a large or
	 *  elongated collider (a long wall) finds it, not just one near its centre.
	 *  BoundingRadius <= 0 stamps the centre cell only. */
	void InsertStatic(FSeinEntityHandle Handle, const FFixedVector& Pos, FFixedPoint BoundingRadius);
	/** Static-rebuild epilogue: clear the dirty flag. */
	void FinishStaticRebuild() { bStaticDirty = false; }

	// ---- Dynamic tier (rebuilt every tick) ----

	/** Drop all dynamic entries (per-tick rebuild prologue). */
	void ClearDynamic();

	/** Batched per-tick dynamic rebuild. Clears the dynamic tier, then for every
	 *  collider computes its footprint-AABB cell coverage (the SAME cell-coverage
	 *  rule the static tier's StampCells uses) into disjoint per-collider storage
	 *  across worker threads (SeinParallelFor — the body reads only its own input
	 *  and writes only its own slot), concatenates the per-collider entries, and
	 *  finally sorts the flat array by (CellKey, Index, Generation). That sort
	 *  CANONICALIZES the result independent of the parallel gather/thread order,
	 *  so the dynamic tier is deterministic and thread-count-independent. The
	 *  sorted-by-CellKey layout lets QueryRadius binary-search the run for a cell. */
	void BuildDynamic(const TArray<FDynamicColliderInput>& Colliders);

	// ---- Query ----

	/** Append all collider handles (from BOTH tiers) within `Radius` of
	 *  `QueryPos`, sorted by handle index ascending. `Exclude` (if valid) is
	 *  filtered out (self-exclusion). Output is appended — caller Resets if it
	 *  wants a fresh array. Like the legacy hash, this is a bucket fan-out: the
	 *  caller does the exact narrow-phase test on the returned candidates.
	 *  Duplicates (a collider stamped into several cells the query spans) are
	 *  removed, so each handle appears at most once. */
	void QueryRadius(
		const FFixedVector& QueryPos,
		FFixedPoint Radius,
		TArray<FSeinEntityHandle>& Out,
		FSeinEntityHandle Exclude = FSeinEntityHandle()) const;

	FFixedPoint GetCellSize() const { return CellSize; }
	int32 NumStaticBuckets() const { return StaticBuckets.Num(); }
	/** Count of (CellKey, Handle) entries in the dynamic sort grid (a footprint-
	 *  stamped collider contributes one entry per cell it covers). */
	int32 NumDynamicEntries() const { return DynamicEntries.Num(); }

private:
	/** One (cell, handle) stamp in the dynamic sort grid. The flat array of these
	 *  is kept sorted by (CellKey, Handle.Index, Handle.Generation) after a build,
	 *  so a cell's entries form a contiguous, binary-searchable run. */
	struct FCellEntry
	{
		int64             CellKey;
		FSeinEntityHandle Handle;
	};

	/** Pack (cellX, cellY) into the int64 key. uint32 reinterpret handles
	 *  negatives without aliasing. */
	static FORCEINLINE int64 MakeKey(int32 CellX, int32 CellY)
	{
		const uint32 X = static_cast<uint32>(CellX);
		const uint32 Y = static_cast<uint32>(CellY);
		return (static_cast<int64>(X) << 32) | static_cast<int64>(Y);
	}

	/** World coord → cell index along one axis. Floor division on raw fp bits
	 *  (deterministic on all platforms); manual floor correction for the
	 *  negative-with-remainder case. */
	int32 ToCell(FFixedPoint WorldCoord, FFixedPoint OriginCoord) const;

	/** Stamp `Handle` into every cell the footprint AABB (Pos ± BoundingRadius)
	 *  covers, in the given tier's (static) buckets. Used by InsertStatic. */
	void StampCells(TMap<int64, TArray<FSeinEntityHandle>>& Buckets,
		FSeinEntityHandle Handle, const FFixedVector& Pos, FFixedPoint BoundingRadius);

	/** Append, into `OutEntries`, one FCellEntry per cell `Handle`'s footprint
	 *  AABB (Pos ± BoundingRadius) covers — the SAME cell-coverage rule as
	 *  StampCells, just emitting flat (CellKey, Handle) pairs into a per-collider
	 *  buffer instead of TMap buckets. Shared by BuildDynamic's parallel gather. */
	void StampCellEntries(TArray<FCellEntry>& OutEntries,
		FSeinEntityHandle Handle, const FFixedVector& Pos, FFixedPoint BoundingRadius) const;

	TMap<int64, TArray<FSeinEntityHandle>> StaticBuckets;
	TArray<FCellEntry>                     DynamicEntries; // sorted by (CellKey, Index, Generation)
	FFixedPoint  CellSize;
	FFixedVector Origin;
	bool         bStaticDirty;
};
