/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWedgeFormation.cpp
 * @brief   Arrowhead: tip forward, arms fanning back-left / back-right, drag-oriented.
 */

#include "Formations/SeinWedgeFormation.h"

FSeinFormationLayout USeinWedgeFormation::BuildFormation_Implementation(
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

	// Facing + center (drag faces forward over the line, body behind; click faces target).
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

	// Member 0 is the tip (at the center). Each subsequent member alternates left/right and
	// steps one rank back per pair: i=1,2 → rank 1 (back-left, back-right), i=3,4 → rank 2,
	// etc. Local +X is forward, so the body uses -X (behind) and ±Y (sideways).
	Layout.Positions.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		FFixedVector LocalOffset = FFixedVector::ZeroVector;
		if (i > 0)
		{
			const int32 Rank = (i + 1) / 2;          // 1,1,2,2,3,3,...
			const int32 Side = (i % 2 == 1) ? -1 : 1; // left, right, left, right, ...
			const FFixedPoint Back    = FFixedPoint::Zero - S * FFixedPoint::FromInt(Rank);
			const FFixedPoint Lateral = S * FFixedPoint::FromInt(Rank * Side);
			LocalOffset = FFixedVector(Back, Lateral, FFixedPoint::Zero);
		}
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		Layout.Positions.Add(ProjectToNavigable(World, Center + WorldOffset, Center));
	}
	return Layout;
}
