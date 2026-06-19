/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLineFormation.cpp
 * @brief   Even spread along the order guide line; perpendicular facing.
 */

#include "Formations/SeinLineFormation.h"

FSeinFormationLayout USeinLineFormation::BuildFormation_Implementation(
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

	// Plain click (no drag guide): still a LINE, not a blob — a minimum-length rank
	// perpendicular to the move direction, centered on the anchor (so single-click line
	// formations spread). A single unit just stands at the anchor.
	if (Target.GuidePoints.Num() < 2)
	{
		const FFixedQuaternion ClickFacing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
		Layout.Facing = ClickFacing;
		Layout.Positions.Reserve(N);
		if (N == 1)
		{
			Layout.Positions.Add(ProjectToNavigable(World, Target.Anchor, Target.Anchor));
			return Layout;
		}
		// Rank axis = perpendicular to the facing's forward, so the rank faces the move dir.
		const FFixedVector Fwd = ClickFacing.RotateVector(FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero));
		FFixedVector Axis(FFixedPoint::Zero - Fwd.Y, Fwd.X, FFixedPoint::Zero);
		Axis = Axis.IsNearlyZero() ? FFixedVector(FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero) : FFixedVector::GetSafeNormal(Axis);
		const FFixedPoint CS     = (InterUnitSpacing > FFixedPoint::Zero) ? InterUnitSpacing : FFixedPoint::FromInt(150);
		const FFixedPoint CLen   = FFixedPoint::FromInt(N - 1) * CS;
		const FFixedPoint CHalf  = CLen / FFixedPoint::Two;
		const FFixedPoint CDenom = FFixedPoint::FromInt(N - 1);
		for (int32 i = 0; i < N; ++i)
		{
			const FFixedPoint Along = (FFixedPoint::FromInt(i) / CDenom) * CLen - CHalf;
			const FFixedVector Pos(Target.Anchor.X + Axis.X * Along, Target.Anchor.Y + Axis.Y * Along, Target.Anchor.Z + Axis.Z * Along);
			Layout.Positions.Add(ProjectToNavigable(World, Pos, Target.Anchor));
		}
		return Layout;
	}

	const FFixedVector Start = Target.GuidePoints[0];
	const FFixedVector End   = Target.GuidePoints.Last();
	const FFixedVector Mid   = (Start + End) / FFixedPoint::Two;

	// Facing: perpendicular to the line on the side away from the current centroid
	// (battle-line), or along the move direction toward the line midpoint.
	FFixedVector LineDir = End - Start;
	LineDir.Z = FFixedPoint::Zero;
	if (bFacePerpendicular && !LineDir.IsNearlyZero())
	{
		const FFixedVector Dir = FFixedVector::GetSafeNormal(LineDir);
		FFixedVector Perp(FFixedPoint::Zero - Dir.Y, Dir.X, FFixedPoint::Zero); // 90° in XY
		FFixedVector AwayFromCentroid = Mid - Target.CurrentCentroid;
		AwayFromCentroid.Z = FFixedPoint::Zero;
		if (FFixedVector::DotProduct(Perp, AwayFromCentroid) < FFixedPoint::Zero)
		{
			Perp = FFixedVector::ZeroVector - Perp;
		}
		Layout.Facing = FacingFromDirection(Perp);
	}
	else
	{
		Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Mid);
	}

	// Spread N along the line, CENTERED on the midpoint, with a guaranteed minimum spacing so
	// units never overlap: the effective length is at least (N-1) * InterUnitSpacing and grows
	// to the drag length when the drag is longer. A single member sits at the midpoint.
	Layout.Positions.Reserve(N);
	if (N == 1)
	{
		Layout.Positions.Add(ProjectToNavigable(World, Mid, Target.Anchor));
		return Layout;
	}

	const FFixedPoint S       = (InterUnitSpacing > FFixedPoint::Zero) ? InterUnitSpacing : FFixedPoint::FromInt(150);
	const FFixedPoint DragLen = LineDir.Size(); // LineDir = End - Start, Z already zeroed above
	const FFixedPoint MinLen  = FFixedPoint::FromInt(N - 1) * S;
	const FFixedPoint EffLen  = (DragLen > MinLen) ? DragLen : MinLen;
	const FFixedVector Dir    = LineDir.IsNearlyZero()
		? FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero) // degenerate guide -> arbitrary axis
		: FFixedVector::GetSafeNormal(LineDir);
	const FFixedPoint HalfLen = EffLen / FFixedPoint::Two;
	const FFixedPoint Denom   = FFixedPoint::FromInt(N - 1);
	for (int32 i = 0; i < N; ++i)
	{
		const FFixedPoint Along = (FFixedPoint::FromInt(i) / Denom) * EffLen - HalfLen; // -HalfLen .. +HalfLen
		const FFixedVector Pos(Mid.X + Dir.X * Along, Mid.Y + Dir.Y * Along, Mid.Z + Dir.Z * Along);
		Layout.Positions.Add(ProjectToNavigable(World, Pos, Target.Anchor));
	}

	return Layout;
}
