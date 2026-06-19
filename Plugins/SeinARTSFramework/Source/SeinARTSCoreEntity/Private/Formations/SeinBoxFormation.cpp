/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBoxFormation.cpp
 * @brief   Rank box sized by the drag: front width = the guide line, depth fills
 *          behind it (toward the centroid) to fit N.
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

	// Need a two-point guide (the front line). Without one (e.g. a plain click), or a
	// degenerate zero-length line, behave like a blob at the anchor.
	const bool bHasLine = Target.GuidePoints.Num() >= 2;
	FFixedVector Start = Target.Anchor, End = Target.Anchor, LineVec = FFixedVector::ZeroVector;
	if (bHasLine)
	{
		Start = Target.GuidePoints[0];
		End   = Target.GuidePoints.Last();
		LineVec = End - Start;
		LineVec.Z = FFixedPoint::Zero;
	}
	if (!bHasLine || LineVec.IsNearlyZero())
	{
		Layout.Facing = ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
		Layout.Positions.Init(Target.Anchor, N);
		return Layout;
	}

	const FFixedPoint  Width = LineVec.Size();
	const FFixedVector Mid((Start.X + End.X) / FFixedPoint::Two,
	                       (Start.Y + End.Y) / FFixedPoint::Two,
	                       (Start.Z + End.Z) / FFixedPoint::Two);

	// Facing: the drag DIRECTION is the authority: face the front's perpendicular by fixed
	// handedness (USeinFormation::DragFacingDir), NOT toward/away any centroid. Ranks fill
	// BEHIND the front line (the drag line is the formation's FRONT edge); with this
	// handedness the back side is +FaceDir, so the box sits behind the line and faces out
	// over it. Drag the line the other way to flip the facing/side.
	const FFixedVector FaceDir = DragFacingDir(Target.GuidePoints);
	const FFixedVector BackDir = FaceDir;
	Layout.Facing = FacingFromDirection(FaceDir);

	// Front-rank column count = how many units fit across the drag width at spacing,
	// capped at N (few units → one sparse rank spanning the drag). Counted by
	// accumulation to avoid a fixed→int conversion.
	const FFixedPoint S = (InterUnitSpacing > FFixedPoint::Zero) ? InterUnitSpacing : FFixedPoint::FromInt(150);
	int32 Columns = 1;
	{
		FFixedPoint Accum = S;
		while (Accum <= Width && Columns < N)
		{
			Accum = Accum + S;
			++Columns;
		}
	}
	if (Columns < 1) { Columns = 1; }
	if (Columns > N) { Columns = N; }

	const FFixedVector Delta = End - Start;
	const FFixedPoint ColDenom = (Columns > 1) ? FFixedPoint::FromInt(Columns - 1) : FFixedPoint::One;

	Layout.Positions.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		const int32 Col = i % Columns;
		const int32 Row = i / Columns;

		// Spread the file across the front line (Start→End); a single column sits at
		// the line midpoint.
		FFixedVector FrontPt;
		if (Columns <= 1)
		{
			FrontPt = Mid;
		}
		else
		{
			const FFixedPoint T = FFixedPoint::FromInt(Col) / ColDenom;
			FrontPt = FFixedVector(Start.X + Delta.X * T, Start.Y + Delta.Y * T, Start.Z + Delta.Z * T);
		}

		// Stack ranks behind the front, toward the centroid.
		const FFixedPoint RowOff = FFixedPoint::FromInt(Row) * S;
		const FFixedVector Pos(FrontPt.X + BackDir.X * RowOff,
		                       FrontPt.Y + BackDir.Y * RowOff,
		                       FrontPt.Z + BackDir.Z * RowOff);

		Layout.Positions.Add(ProjectToNavigable(World, Pos, Target.Anchor));
	}

	return Layout;
}
