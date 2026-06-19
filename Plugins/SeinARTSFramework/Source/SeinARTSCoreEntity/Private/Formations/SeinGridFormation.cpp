/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinGridFormation.cpp
 * @brief   Uniform square-ish grid layout (moved from the default broker resolver's
 *          ResolvePositions formation-spread path).
 */

#include "Formations/SeinGridFormation.h"

FSeinFormationLayout USeinGridFormation::BuildFormation_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target)
{
	FSeinFormationLayout Layout;
	const int32 N = Members.Num();
	// A right-click-drag rotates the grid to face the drag perpendicular (fixed handedness),
	// same as the box/column/wedge/ring; a plain click keeps the move-target facing.
	Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
	const FFixedVector DragFace = DragFacingDir(Target.GuidePoints);
	if (!DragFace.IsNearlyZero())
	{
		Layout.Facing = FacingFromDirection(DragFace);
	}
	if (N == 0) return Layout;

	const FFixedVector Anchor = Target.Anchor;
	const FFixedQuaternion Facing = Layout.Facing;
	const FFixedPoint Spacing = InterUnitSpacing;

	// Uniform square-ish grid: side = ceil(sqrt(N)), iterate row/column centered on
	// the anchor with InterUnitSpacing. Formation-local axes: X forward, Y right.
	int32 Side = 1;
	while (Side * Side < N) ++Side;
	const FFixedPoint HalfExtent = (FFixedPoint::FromInt(Side - 1) * Spacing) / FFixedPoint::Two;

	Layout.Positions.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Col = i % Side;
		const int32 Row = i / Side;

		const FFixedVector LocalOffset(
			FFixedPoint::FromInt(Row) * Spacing - HalfExtent,
			FFixedPoint::FromInt(Col) * Spacing - HalfExtent,
			FFixedPoint::Zero);

		// Rotate by Facing, translate by Anchor, then project to a passable cell so
		// grids spreading off platforms / past nav edges don't strand members on
		// impassable terrain. Failure → fall back to Anchor (passable by definition).
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		const FFixedVector SlotPos = ProjectToNavigable(World, Anchor + WorldOffset, Anchor);
		Layout.Positions.Add(SlotPos);
	}

	return Layout;
}
