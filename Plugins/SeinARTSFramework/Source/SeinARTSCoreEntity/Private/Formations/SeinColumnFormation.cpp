/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinColumnFormation.cpp
 * @brief   Single-file column trailing behind the lead, oriented to the drag.
 */

#include "Formations/SeinColumnFormation.h"

FSeinFormationLayout USeinColumnFormation::BuildFormation_Implementation(
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

	// Facing + center. A drag faces FORWARD over the guide line (the NEGATED perpendicular,
	// so the body trails BEHIND it — the same convention squads and the box use) and centers
	// on the line midpoint. A plain click faces the move target and centers on the anchor.
	const FFixedVector DragPerp = DragFacingDir(Target.GuidePoints);
	const bool bDrag = !DragPerp.IsNearlyZero();
	const FFixedVector Center = bDrag
		? (Target.GuidePoints[0] + Target.GuidePoints.Last()) / FFixedPoint::Two
		: Target.Anchor;
	Layout.Facing = bDrag
		? FacingFromDirection(FFixedVector::ZeroVector - DragPerp)
		: ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);

	const FFixedQuaternion Facing = Layout.Facing;
	const FFixedPoint S = (InterUnitSpacing > FFixedPoint::Zero) ? InterUnitSpacing : FFixedPoint::FromInt(150);

	// Lead at the center, each successive member S further back (local -X = opposite the
	// facing), rotated into world space and nav-projected.
	Layout.Positions.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FFixedVector LocalOffset(FFixedPoint::Zero - S * FFixedPoint::FromInt(i),
		                               FFixedPoint::Zero, FFixedPoint::Zero);
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		Layout.Positions.Add(ProjectToNavigable(World, Center + WorldOffset, Center));
	}
	return Layout;
}
