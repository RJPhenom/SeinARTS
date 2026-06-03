/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSpatialHash.cpp
 */

#include "Simulation/SeinSpatialHash.h"

FSeinSpatialHash::FSeinSpatialHash()
	: CellSize(FFixedPoint::Zero)
	, Origin(FFixedVector::ZeroVector)
{
}

void FSeinSpatialHash::Initialize(FFixedPoint InCellSize, FFixedVector InOrigin)
{
	CellSize = InCellSize;
	Origin = InOrigin;
	Buckets.Reset();
}

void FSeinSpatialHash::Clear()
{
	Buckets.Reset();
}

int32 FSeinSpatialHash::ToCell(FFixedPoint WorldCoord, FFixedPoint OriginCoord) const
{
	// Cell index = floor((WorldCoord - OriginCoord) / CellSize). Done on
	// raw int64 fp bits — both numerator and denominator are scaled by 2^32
	// equally so the divide gives the correct integer cell number directly.
	// Plain C++ integer division truncates toward zero; we add a manual
	// floor correction for the negative-with-remainder case so cell -1 is
	// distinct from cell 0 on the negative side of origin.
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

void FSeinSpatialHash::Insert(FSeinEntityHandle Handle, const FFixedVector& Pos)
{
	if (CellSize <= FFixedPoint::Zero) return;
	const int32 CellX = ToCell(Pos.X, Origin.X);
	const int32 CellY = ToCell(Pos.Y, Origin.Y);
	const int64 Key = MakeKey(CellX, CellY);
	Buckets.FindOrAdd(Key).Add(Handle);
}

void FSeinSpatialHash::QueryRadius(
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

	const FFixedPoint RadiusSq = Radius * Radius;
	const int32 SortStart = Out.Num();

	for (int32 CY = MinY; CY <= MaxY; ++CY)
	{
		for (int32 CX = MinX; CX <= MaxX; ++CX)
		{
			const int64 Key = MakeKey(CX, CY);
			const TArray<FSeinEntityHandle>* Bucket = Buckets.Find(Key);
			if (!Bucket) continue;

			for (const FSeinEntityHandle& H : *Bucket)
			{
				if (H == Exclude) continue;
				// Note: the inserted point is the handle's tracked position
				// from the most recent Insert. For a true distance check the
				// caller already has the world position via component
				// lookup — but the bucket fan-out alone is usually a tight
				// enough first-pass that we keep this loop simple. Caller
				// can do the exact distance test if it cares (most do).
				Out.Add(H);
			}
		}
	}

	// Sort the appended portion by handle index ascending — defeats TMap
	// bucket-iteration nondeterminism. Stable sort not needed because
	// distinct handles are never equal under the comparator.
	if (Out.Num() > SortStart + 1)
	{
		TArrayView<FSeinEntityHandle> View(Out.GetData() + SortStart, Out.Num() - SortStart);
		View.Sort([](const FSeinEntityHandle& A, const FSeinEntityHandle& B)
		{
			return A.Index < B.Index;
		});
	}

	// Suppress unused-variable warning for RadiusSq — caller will perform
	// the exact distance filter using component data they look up anyway.
	(void)RadiusSq;
}
