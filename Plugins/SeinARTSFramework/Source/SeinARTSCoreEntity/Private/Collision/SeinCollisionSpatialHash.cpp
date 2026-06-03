/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionSpatialHash.cpp
 */

#include "Collision/SeinCollisionSpatialHash.h"

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
	DynamicBuckets.Reset();
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
	DynamicBuckets.Reset();
}

void FSeinCollisionSpatialHash::InsertDynamic(FSeinEntityHandle Handle, const FFixedVector& Pos, FFixedPoint BoundingRadius)
{
	StampCells(DynamicBuckets, Handle, Pos, BoundingRadius);
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
			if (const TArray<FSeinEntityHandle>* Bucket = DynamicBuckets.Find(Key))
			{
				for (const FSeinEntityHandle& H : *Bucket)
				{
					if (H == Exclude) continue;
					Out.Add(H);
				}
			}
		}
	}

	// Sort the appended portion by handle index ascending (defeats TMap
	// bucket-iteration nondeterminism), then collapse adjacent duplicates — a
	// footprint-stamped collider appears in every cell it covers, so a query
	// spanning several of those cells would otherwise return it multiple times.
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
