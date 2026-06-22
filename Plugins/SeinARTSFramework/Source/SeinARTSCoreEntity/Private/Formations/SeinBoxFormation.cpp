/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBoxFormation.cpp
 * @brief   Footprint-aware rank box. Shares the Grid's packer
 *          (USeinFormation::PackFootprints) but sets the front WIDTH from the drag
 *          length instead of a square aspect — biggest units front-and-centre, the
 *          rest filling the flanks and ranks behind. A plain click → a square-ish
 *          block on the cursor (same as the grid). The drag line is the FRONT edge;
 *          the body packs BEHIND it. InterUnitSpacing is the MINIMUM cell size, so a
 *          designer can open the ranks up beyond the tight footprint packing.
 */

#include "Formations/SeinBoxFormation.h"

FSeinFormationLayout USeinBoxFormation::BuildFormation_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target)
{
	FSeinFormationLayout Layout;
	const int32 N = Members.Num();
	if (N == 0)
	{
		Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
		return Layout;
	}

	GatherFootprintRadii(World, Members, Layout.Radii);

	// Drag vs click. A drag draws the FRONT line: its length sets the box's front width and its
	// perpendicular (fixed handedness, DragFacingDir) the facing; the body packs BEHIND the line,
	// centred on the line's midpoint so the front rank spans the drawn width. A plain click → a
	// square-ish block centred on the cursor, facing the move direction (identical to the grid).
	const FFixedVector DragFace = DragFacingDir(Target.GuidePoints);
	const bool bDrag = !DragFace.IsNearlyZero();

	FFixedVector Center;
	FFixedPoint  FrontWidth = FFixedPoint::Zero; // <= 0 → square-ish
	if (bDrag)
	{
		const FFixedVector Start = Target.GuidePoints[0];
		const FFixedVector End   = Target.GuidePoints.Last();
		Center = (Start + End) / FFixedPoint::Two;
		FFixedVector LineVec = End - Start; LineVec.Z = FFixedPoint::Zero;
		FrontWidth = LineVec.Size();
		Layout.Facing = FacingFromDirection(DragFace);
	}
	else
	{
		Center = Target.Anchor;
		Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
	}
	const FFixedQuaternion Facing = Layout.Facing;

	// Same footprint packer as the grid — only the front width differs (drag width vs square-ish).
	// InterUnitSpacing is the MINIMUM cell, opening the ranks beyond the tight footprint pack.
	FSeinFootprintPacking Pack;
	PackFootprints(Layout.Radii, FrontWidth, InterUnitSpacing, Pack);
	const FFixedPoint Cell = Pack.Cell;

	// Centre on the occupied bounding box; row 0 → FRONT. Drag → front rank on the line, body behind;
	// click → centred block, front toward the move dir. (Identical anchoring to the grid.)
	int32 MinR = MAX_int32, MaxR = -1, MinC = MAX_int32, MaxC = -1;
	for (int32 i = 0; i < N; ++i)
	{
		const int32 s = Pack.Span[i];
		MinR = FMath::Min(MinR, Pack.BlockRow[i]); MaxR = FMath::Max(MaxR, Pack.BlockRow[i] + s - 1);
		MinC = FMath::Min(MinC, Pack.BlockCol[i]); MaxC = FMath::Max(MaxC, Pack.BlockCol[i] + s - 1);
	}
	const int32 MidRow2 = MinR + MaxR + 1;
	const int32 MidCol2 = MinC + MaxC + 1;

	Layout.Positions.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 s = Pack.Span[i];
		const FFixedPoint LocalX = bDrag
			? FFixedPoint::FromInt(2 * Pack.BlockRow[i] + s) * Cell / FFixedPoint::Two
			: FFixedPoint::FromInt(MidRow2 - (2 * Pack.BlockRow[i] + s)) * Cell / FFixedPoint::Two;
		const FFixedPoint LocalY = FFixedPoint::FromInt((2 * Pack.BlockCol[i] + s) - MidCol2) * Cell / FFixedPoint::Two;
		const FFixedVector WorldOffset = Facing.RotateVector(FFixedVector(LocalX, LocalY, FFixedPoint::Zero));
		Layout.Positions[i] = ProjectToNavigable(World, Center + WorldOffset, Center);
	}

	return Layout;
}
