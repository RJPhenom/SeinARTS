/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionSpatialHash.cpp
 */

#include "Collision/SeinCollisionSpatialHash.h"
#include "Core/SeinParallel.h"
#include "Algo/Sort.h"
#include "Algo/BinarySearch.h"

FSeinCollisionSpatialHash::FSeinCollisionSpatialHash()
	: CellSize(FFixedPoint::Zero)
	, Origin(FFixedVector::ZeroVector)
	, bStaticDirty(true)
{
}

void FSeinCollisionSpatialHash::Initialize(FFixedPoint InCellSize, FFixedVector InOrigin)
{
	CellSize = InCellSize;
	Origin = InOrigin;
	StaticBuckets.Reset();
	DynamicEntries.Reset();
	bStaticDirty = true;
}

int32 FSeinCollisionSpatialHash::ToCell(FFixedPoint WorldCoord, FFixedPoint OriginCoord) const
{
	// Cell index = floor((WorldCoord - OriginCoord) / CellSize), done on raw
	// int64 fp bits (numerator and denominator both scaled by 2^32, so the
	// divide yields the integer cell directly). C++ integer division truncates
	// toward zero; add a floor correction for the negative-with-remainder case
	// so cell -1 stays distinct from cell 0 on the negative side of origin.
	if (CellSize <= FFixedPoint::Zero) return 0;
	const int64 RawDiff = static_cast<int64>(WorldCoord) - static_cast<int64>(OriginCoord);
	const int64 RawCellSize = static_cast<int64>(CellSize);
	int64 Cell = RawDiff / RawCellSize;
	if ((RawDiff % RawCellSize != 0) && ((RawDiff < 0) != (RawCellSize < 0)))
	{
		Cell -= 1;
	}
	return static_cast<int32>(Cell);
}

void FSeinCollisionSpatialHash::StampCells(
	TMap<int64, TArray<FSeinEntityHandle>>& Buckets,
	FSeinEntityHandle Handle, const FFixedVector& Pos, FFixedPoint BoundingRadius)
{
	if (CellSize <= FFixedPoint::Zero) return;

	if (BoundingRadius <= FFixedPoint::Zero)
	{
		// No footprint — stamp the single centre cell.
		const int64 Key = MakeKey(ToCell(Pos.X, Origin.X), ToCell(Pos.Y, Origin.Y));
		Buckets.FindOrAdd(Key).Add(Handle);
		return;
	}

	// Stamp every cell the footprint AABB (Pos ± BoundingRadius) covers.
	const int32 MinX = ToCell(Pos.X - BoundingRadius, Origin.X);
	const int32 MaxX = ToCell(Pos.X + BoundingRadius, Origin.X);
	const int32 MinY = ToCell(Pos.Y - BoundingRadius, Origin.Y);
	const int32 MaxY = ToCell(Pos.Y + BoundingRadius, Origin.Y);
	for (int32 CY = MinY; CY <= MaxY; ++CY)
	{
		for (int32 CX = MinX; CX <= MaxX; ++CX)
		{
			Buckets.FindOrAdd(MakeKey(CX, CY)).Add(Handle);
		}
	}
}

void FSeinCollisionSpatialHash::StampCellEntries(
	TArray<FCellEntry>& OutEntries,
	FSeinEntityHandle Handle, const FFixedVector& Pos, FFixedPoint BoundingRadius) const
{
	// Flat-array twin of StampCells: emit one (CellKey, Handle) entry per cell the
	// footprint AABB covers, using the IDENTICAL cell-coverage rule so the dynamic
	// tier's per-cell membership matches what TMap stamping would have produced.
	if (CellSize <= FFixedPoint::Zero) return;

	if (BoundingRadius <= FFixedPoint::Zero)
	{
		// No footprint — stamp the single centre cell.
		const int64 Key = MakeKey(ToCell(Pos.X, Origin.X), ToCell(Pos.Y, Origin.Y));
		OutEntries.Add(FCellEntry{ Key, Handle });
		return;
	}

	const int32 MinX = ToCell(Pos.X - BoundingRadius, Origin.X);
	const int32 MaxX = ToCell(Pos.X + BoundingRadius, Origin.X);
	const int32 MinY = ToCell(Pos.Y - BoundingRadius, Origin.Y);
	const int32 MaxY = ToCell(Pos.Y + BoundingRadius, Origin.Y);
	for (int32 CY = MinY; CY <= MaxY; ++CY)
	{
		for (int32 CX = MinX; CX <= MaxX; ++CX)
		{
			OutEntries.Add(FCellEntry{ MakeKey(CX, CY), Handle });
		}
	}
}

void FSeinCollisionSpatialHash::ClearStatic()
{
	StaticBuckets.Reset();
}

void FSeinCollisionSpatialHash::InsertStatic(FSeinEntityHandle Handle, const FFixedVector& Pos, FFixedPoint BoundingRadius)
{
	StampCells(StaticBuckets, Handle, Pos, BoundingRadius);
}

void FSeinCollisionSpatialHash::ClearDynamic()
{
	DynamicEntries.Reset();
}

void FSeinCollisionSpatialHash::BuildDynamic(const TArray<FDynamicColliderInput>& Colliders)
{
	DynamicEntries.Reset();
	if (CellSize <= FFixedPoint::Zero || Colliders.Num() == 0) return;

	// --- Parallel gather (per-collider, disjoint) ---
	// Each collider's footprint cell-stamp is independent: it reads only its own
	// FDynamicColliderInput and writes only its own per-collider entry buffer.
	// That satisfies the SeinParallelFor contract (immutable reads + disjoint
	// per-slot writes), so the gather is deterministic regardless of thread count.
	// We give each collider its own scratch buffer (PerColliderEntries[i]) so no
	// two worker threads ever touch the same array — the concatenation that flattens
	// them is serial below. (Sein.Sim.Parallel 0 / a small batch runs this serially;
	// the result is bit-identical either way.)
	const int32 NumColliders = Colliders.Num();
	TArray<TArray<FCellEntry>> PerColliderEntries;
	PerColliderEntries.SetNum(NumColliders);

	SeinParallelFor(NumColliders, [&](int32 Index)
	{
		const FDynamicColliderInput& In = Colliders[Index];
		StampCellEntries(PerColliderEntries[Index], In.Handle, In.Pos, In.BoundingRadius);
	});

	// --- Concatenate (serial) ---
	int32 Total = 0;
	for (const TArray<FCellEntry>& Bucket : PerColliderEntries) { Total += Bucket.Num(); }
	DynamicEntries.Reserve(Total);
	for (const TArray<FCellEntry>& Bucket : PerColliderEntries)
	{
		DynamicEntries.Append(Bucket);
	}

	// --- Canonicalize (serial) ---
	// Sort by (CellKey, then Handle.Index, then Handle.Generation). This makes the
	// flat array a stable, thread-count-independent canonical form: the gather order
	// (which collider landed where) is erased, so two clients with different worker
	// counts produce byte-identical DynamicEntries. Sorting CellKey-first also groups
	// each cell's entries into one contiguous run for QueryRadius's binary search.
	Algo::Sort(DynamicEntries, [](const FCellEntry& A, const FCellEntry& B)
	{
		if (A.CellKey != B.CellKey)               return A.CellKey < B.CellKey;
		if (A.Handle.Index != B.Handle.Index)     return A.Handle.Index < B.Handle.Index;
		return A.Handle.Generation < B.Handle.Generation;
	});
}

void FSeinCollisionSpatialHash::QueryRadius(
	const FFixedVector& QueryPos,
	FFixedPoint Radius,
	TArray<FSeinEntityHandle>& Out,
	FSeinEntityHandle Exclude) const
{
	if (CellSize <= FFixedPoint::Zero || Radius <= FFixedPoint::Zero) return;

	// Cell range covering the query AABB (radius-padded both axes).
	const int32 MinX = ToCell(QueryPos.X - Radius, Origin.X);
	const int32 MaxX = ToCell(QueryPos.X + Radius, Origin.X);
	const int32 MinY = ToCell(QueryPos.Y - Radius, Origin.Y);
	const int32 MaxY = ToCell(QueryPos.Y + Radius, Origin.Y);

	const int32 SortStart = Out.Num();

	for (int32 CY = MinY; CY <= MaxY; ++CY)
	{
		for (int32 CX = MinX; CX <= MaxX; ++CX)
		{
			const int64 Key = MakeKey(CX, CY);
			if (const TArray<FSeinEntityHandle>* Bucket = StaticBuckets.Find(Key))
			{
				for (const FSeinEntityHandle& H : *Bucket)
				{
					if (H == Exclude) continue;
					Out.Add(H);
				}
			}

			// Dynamic tier: DynamicEntries is sorted by CellKey first, so this cell's
			// entries form a contiguous run. Binary-search the run start (LowerBound on
			// CellKey) and walk forward while CellKey matches — same membership the old
			// DynamicBuckets.Find(Key) produced, just out of a sort grid. (LowerBound on
			// CellKey lands at the first entry whose CellKey >= Key; the run may be empty
			// if no entry has exactly Key, exactly mirroring a missing TMap bucket.)
			const int32 RunStart = Algo::LowerBound(DynamicEntries, Key,
				[](const FCellEntry& E, int64 K) { return E.CellKey < K; });
			for (int32 i = RunStart; i < DynamicEntries.Num() && DynamicEntries[i].CellKey == Key; ++i)
			{
				const FSeinEntityHandle& H = DynamicEntries[i].Handle;
				if (H == Exclude) continue;
				Out.Add(H);
			}
		}
	}

	// Sort the appended portion by handle index ascending (defeats bucket/run
	// iteration nondeterminism), then collapse adjacent duplicates — a footprint-
	// stamped collider appears in every cell it covers, so a query spanning several
	// of those cells would otherwise return it multiple times. (Unchanged from the
	// TMap path: the output is a pure function of the SET of handles collected, so
	// switching the dynamic tier's storage layout cannot alter this result.)
	const int32 Count = Out.Num() - SortStart;
	if (Count > 1)
	{
		TArrayView<FSeinEntityHandle> View(Out.GetData() + SortStart, Count);
		View.Sort([](const FSeinEntityHandle& A, const FSeinEntityHandle& B)
		{
			return A.Index < B.Index;
		});

		// In-place unique over the sorted appended range (compare full handle:
		// index + generation, though distinct live entities never share index).
		int32 Write = SortStart + 1;
		for (int32 Read = SortStart + 1; Read < Out.Num(); ++Read)
		{
			if (!(Out[Read] == Out[Write - 1]))
			{
				Out[Write++] = Out[Read];
			}
		}
		Out.SetNum(Write, EAllowShrinking::No);
	}
}
