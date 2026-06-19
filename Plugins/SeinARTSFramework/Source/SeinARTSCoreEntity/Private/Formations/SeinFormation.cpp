/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormation.cpp
 * @brief   USeinFormation base: shared facing / nav-projection helpers and the
 *          default blob layout.
 */

#include "Formations/SeinFormation.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Math/MathLib.h"
#include "Types/FixedPoint.h"

FFixedQuaternion USeinFormation::FacingFromDirection(FFixedVector DirectionXY)
{
	FFixedVector Flat(DirectionXY.X, DirectionXY.Y, FFixedPoint::Zero);
	if (Flat.IsNearlyZero()) return FFixedQuaternion::Identity;
	const FFixedPoint Yaw = SeinMath::Atan2(Flat.Y, Flat.X);
	return FFixedQuaternion::FromAxisAndAngle(FFixedVector::UpVector, Yaw);
}

FFixedVector USeinFormation::DragFacingDir(const TArray<FFixedVector>& GuidePoints)
{
	if (GuidePoints.Num() < 2) return FFixedVector::ZeroVector;
	FFixedVector Line = GuidePoints.Last() - GuidePoints[0];
	Line.Z = FFixedPoint::Zero;
	if (Line.IsNearlyZero()) return FFixedVector::ZeroVector;
	const FFixedVector Dir = FFixedVector::GetSafeNormal(Line);
	// Drag DIRECTION is the authority: the perpendicular on a fixed handedness
	// (Start->End rotated a quarter turn). No centroid: a drag's facing must not depend
	// on where the units stand. Drag the line the other way to flip the side.
	return FFixedVector(FFixedPoint::Zero - Dir.Y, Dir.X, FFixedPoint::Zero);
}

FFixedQuaternion USeinFormation::ComputeFormationFacing(
	FFixedVector CurrentCentroid,
	FFixedQuaternion CurrentFacing,
	FFixedVector TargetLocation)
{
	FFixedVector ToTarget = TargetLocation - CurrentCentroid;
	ToTarget.Z = FFixedPoint::Zero; // 2D — RTS top-down, ignore vertical

	// Move-to-where-we-are: keep current facing rather than degenerate-quat'ing.
	if (ToTarget.IsNearlyZero()) return CurrentFacing;

	// Facing ALWAYS rotates to face the move direction — the formation pivots to
	// align with where it's going, every move, including a straight 180° backpedal.
	return FacingFromDirection(FFixedVector::GetSafeNormal(ToTarget));
}

FFixedVector USeinFormation::ProjectToNavigable(
	USeinWorldSubsystem* World,
	FFixedVector Position,
	FFixedVector Fallback)
{
	if (World && World->NavProjectResolver.IsBound())
	{
		FFixedVector Projected;
		if (World->NavProjectResolver.Execute(Position, Projected)) return Projected;
		return Fallback;
	}
	return Position;
}

FSeinFormationLayout USeinFormation::BuildFormation_Implementation(
	USeinWorldSubsystem* World,
	const TArray<FSeinEntityHandle>& Members,
	const FSeinOrderTarget& Target)
{
	// Default: BLOB. Every member shares the one (already nav-projected) anchor —
	// the AoE/SC2/CoH model; the hard collision floor packs them on arrival. Facing
	// rotates to face the move direction. USeinBlobFormation inherits this as-is.
	FSeinFormationLayout Layout;
	Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
	Layout.Positions.Init(Target.Anchor, Members.Num());
	return Layout;
}
