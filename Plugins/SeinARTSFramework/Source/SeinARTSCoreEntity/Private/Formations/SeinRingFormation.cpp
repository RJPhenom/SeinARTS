/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinRingFormation.cpp
 * @brief   Evenly spaced ring about the anchor; deterministic fixed-point trig.
 */

#include "Formations/SeinRingFormation.h"
#include "Math/MathLib.h"

FSeinFormationLayout USeinRingFormation::BuildFormation_Implementation(
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

	// Facing + center (drag faces forward over the line; click faces the move target).
	const FFixedVector DragPerp = DragFacingDir(Target.GuidePoints);
	const bool bDrag = !DragPerp.IsNearlyZero();
	const FFixedVector Center = bDrag
		? (Target.GuidePoints[0] + Target.GuidePoints.Last()) / FFixedPoint::Two
		: Target.Anchor;
	Layout.Facing = bDrag
		? FacingFromDirection(FFixedVector::ZeroVector - DragPerp)
		: ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);

	// One member just stands at the center (a 1-unit ring is degenerate).
	if (N == 1)
	{
		Layout.Positions.Add(ProjectToNavigable(World, Center, Center));
		return Layout;
	}

	const FFixedQuaternion Facing = Layout.Facing;
	const FFixedPoint S = (InterUnitSpacing > FFixedPoint::Zero) ? InterUnitSpacing : FFixedPoint::FromInt(150);

	// Radius = max(default, half the drag length). Default = the no-overlap minimum
	// (circumference ~ N*S, so R = N*S / 2π, floored at S). The ring never shrinks below that,
	// but GROWS with the drag so its diameter = max(default diameter, drag length).
	FFixedPoint R = (S * FFixedPoint::FromInt(N)) / FFixedPoint::TwoPi;
	if (R < S) { R = S; }
	if (Target.GuidePoints.Num() >= 2)
	{
		FFixedVector DragVec = Target.GuidePoints.Last() - Target.GuidePoints[0];
		DragVec.Z = FFixedPoint::Zero;
		const FFixedPoint DragRadius = DragVec.Size() / FFixedPoint::Two;
		if (DragRadius > R) { R = DragRadius; }
	}

	// Even angular spacing around the circle; deterministic fixed-point sin/cos.
	Layout.Positions.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const FFixedPoint Angle = (FFixedPoint::TwoPi * FFixedPoint::FromInt(i)) / FFixedPoint::FromInt(N);
		const FFixedVector LocalOffset(R * SeinMath::Cos(Angle), R * SeinMath::Sin(Angle), FFixedPoint::Zero);
		const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
		Layout.Positions.Add(ProjectToNavigable(World, Center + WorldOffset, Center));
	}
	return Layout;
}
