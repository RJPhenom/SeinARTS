/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGridFormation.cpp
 * @brief   Footprint-aware tight SQUARE grid. Delegates the packing to the shared
 *          USeinFormation::PackFootprints (cell = smallest footprint; each unit a
 *          span×span block; biggest blocks front-and-centre; 1×1s fill row-major),
 *          asking for a square-ish front width (DesiredFrontWidth <= 0). Each unit's
 *          dot is its block CENTRE, so the footprint ring covers its whole block.
 *          Row 0 is the FRONT; a plain click centres on the anchor facing the move
 *          dir, a drag puts the front rank on the drag line with the body behind it.
 *          (USeinBoxFormation shares the same packer but at the drag WIDTH.)
 */

#include "Formations/SeinGridFormation.h"

FSeinFormationLayout USeinGridFormation::BuildFormation_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target)
{
	FSeinFormationLayout Layout;
	const int32 N = Members.Num();
	// A right-click-drag rotates the grid to face the drag perpendicular (fixed handedness);
	// a plain click keeps the move-target facing.
	Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
	const FFixedVector DragFace = DragFacingDir(Target.GuidePoints);
	if (!DragFace.IsNearlyZero()) { Layout.Facing = FacingFromDirection(DragFace); }
	if (N == 0) return Layout;

	const FFixedVector Anchor = Target.Anchor;
	const FFixedQuaternion Facing = Layout.Facing;
	GatherFootprintRadii(World, Members, Layout.Radii);

	// Shared footprint packer, square-ish (DesiredFrontWidth <= 0), tight (MinCell 0).
	FSeinFootprintPacking Pack;
	PackFootprints(Layout.Radii, FFixedPoint::Zero, FFixedPoint::Zero, Pack);
	const FFixedPoint Cell = Pack.Cell;

	// Centre on the occupied bounding box; row 0 → FRONT. Click → centred, front toward the move dir.
	// Drag → the drag line is the FRONT, body extends behind it.
	int32 MinR = MAX_int32, MaxR = -1, MinC = MAX_int32, MaxC = -1;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 s = Pack.Span[i];
		MinR = FMath::Min(MinR, Pack.BlockRow[i]); MaxR = FMath::Max(MaxR, Pack.BlockRow[i] + s - 1);
		MinC = FMath::Min(MinC, Pack.BlockCol[i]); MaxC = FMath::Max(MaxC, Pack.BlockCol[i] + s - 1);
	}
	const int32 MidRow2 = MinR + MaxR + 1;
	const int32 MidCol2 = MinC + MaxC + 1;
	const bool  bDrag   = !DragFace.IsNearlyZero();

	Layout.Positions.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 s = Pack.Span[i];
		const FFixedPoint LocalX = bDrag
			? FFixedPoint::FromInt(2 * Pack.BlockRow[i] + s) * Cell / FFixedPoint::Two
			: FFixedPoint::FromInt(MidRow2 - (2 * Pack.BlockRow[i] + s)) * Cell / FFixedPoint::Two;
		const FFixedPoint LocalY = FFixedPoint::FromInt((2 * Pack.BlockCol[i] + s) - MidCol2) * Cell / FFixedPoint::Two;
		const FFixedVector WorldOffset = Facing.RotateVector(FFixedVector(LocalX, LocalY, FFixedPoint::Zero));
		Layout.Positions[i] = ProjectToNavigable(World, Anchor + WorldOffset, Anchor);
	}

	return Layout;
}
