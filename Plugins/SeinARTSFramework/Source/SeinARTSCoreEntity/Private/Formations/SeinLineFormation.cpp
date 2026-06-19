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

	// Need two distinct guide points to define a line; otherwise (a plain click that
	// produced no drag guide) behave like a blob at the anchor.
	if (Target.GuidePoints.Num() < 2)
	{
		Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
		Layout.Positions.Init(Target.Anchor, N);
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

	// Single member sits at the line midpoint; otherwise distribute evenly Start→End.
	Layout.Positions.Reserve(N);
	if (N == 1)
	{
		Layout.Positions.Add(ProjectToNavigable(World, Mid, Target.Anchor));
		return Layout;
	}

	const FFixedVector Delta = End - Start;
	const FFixedPoint Denom = FFixedPoint::FromInt(N - 1);
	for (int32 i = 0; i < N; ++i)
	{
		const FFixedPoint T = FFixedPoint::FromInt(i) / Denom;
		const FFixedVector Pos(Start.X + Delta.X * T, Start.Y + Delta.Y * T, Start.Z + Delta.Z * T);
		Layout.Positions.Add(ProjectToNavigable(World, Pos, Target.Anchor));
	}

	return Layout;
}
