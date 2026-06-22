/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquareFormation.cpp
 * @brief   Complete hollow square about the anchor — ONE continuous evenly-spaced outline with a member
 *          on every CORNER, each side marching from its corner (variant footprint spacing), centre left
 *          EMPTY. A drag sets the outer half-side; extra members spill into tighter inner squares.
 */

#include "Formations/SeinSquareFormation.h"

FSeinFormationLayout USeinSquareFormation::BuildFormation_Implementation(
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

	// Footprint radii drive spacing + preview dot sizing.
	GatherFootprintRadii(World, Members, Layout.Radii);

	// Facing + center (drag faces forward over the line; click faces the move target).
	const FFixedVector DragPerp = DragFacingDir(Target.GuidePoints);
	const bool bDrag = !DragPerp.IsNearlyZero();
	const FFixedVector Center = bDrag
		? (Target.GuidePoints[0] + Target.GuidePoints.Last()) / FFixedPoint::Two
		: Target.Anchor;
	Layout.Facing = bDrag
		? FacingFromDirection(FFixedVector::ZeroVector - DragPerp)
		: ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);

	// A 1-unit square is degenerate — the member just stands at the center.
	if (N == 1)
	{
		Layout.Positions.Add(ProjectToNavigable(World, Center, Center));
		return Layout;
	}

	const FFixedQuaternion Facing = Layout.Facing;

	// Effective footprint radius per member = real radius + half the optional InterUnitSpacing margin.
	// The biggest effective DIAMETER sets the gap between nested squares AND the minimum half-side floor.
	const FFixedPoint HalfSpace = InterUnitSpacing / FFixedPoint::Two;
	FFixedPoint MaxEffR = FFixedPoint::Zero, SumEffR = FFixedPoint::Zero;
	TArray<FFixedPoint> EffR; EffR.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		EffR[i] = Layout.Radii[i] + HalfSpace;
		if (EffR[i] < FFixedPoint::Zero) { EffR[i] = FFixedPoint::Zero; }
		SumEffR = SumEffR + EffR[i];
		if (EffR[i] > MaxEffR) { MaxEffR = EffR[i]; }
	}
	FFixedPoint RadialGap = MaxEffR * FFixedPoint::Two;          // nested squares clear by a full diameter
	if (RadialGap <= FFixedPoint::Zero) { RadialGap = FFixedPoint::FromInt(50); }
	const FFixedPoint MinHalfSide = RadialGap;                   // floor

	// Outer half-side. A DRAG sets it directly (honor the drag fully — expand uncapped). A plain CLICK
	// sizes ONE tight square whose perimeter (8h) holds everyone: 8h = Σ diameter = 2·SumEffR → h =
	// SumEffR/4. Straight sides mean euclidean == along-side distance (no asin correction, unlike ring).
	FFixedPoint OuterHalfSide;
	if (bDrag)
	{
		FFixedVector DragVec = Target.GuidePoints.Last() - Target.GuidePoints[0]; DragVec.Z = FFixedPoint::Zero;
		OuterHalfSide = DragVec.Size() / FFixedPoint::Two;
	}
	else
	{
		OuterHalfSide = SumEffR / FFixedPoint::FromInt(4);
	}

	// N-aware shrink floor: grow the minimum half-side until the nested squares (perimeter 8h each, down
	// to MinHalfSide) hold ALL members, so a hard shrink lands on the tightest MULTI-square packing rather
	// than one overflowing square (a blob). Honor-the-drag still applies above this floor.
	auto FitsAll = [&](FFixedPoint OuterH) -> bool
	{
		FFixedPoint h = OuterH; int32 u = 0;
		while (u < N && h >= MinHalfSide)
		{
			const FFixedPoint Perim = h * FFixedPoint::FromInt(8);
			const int32 St = u; FFixedPoint Used = FFixedPoint::Zero;
			while (u < N)
			{
				const FFixedPoint D = EffR[u] * FFixedPoint::Two;
				if (u > St && (Used + D) > Perim) { break; }
				Used = Used + D; ++u;
			}
			h = h - RadialGap;
		}
		return u >= N;
	};
	FFixedPoint MinOuter = MinHalfSide;
	for (int32 Guard = 0; Guard < 256 && !FitsAll(MinOuter); ++Guard) { MinOuter = MinOuter + RadialGap; }
	if (OuterHalfSide < MinOuter) { OuterHalfSide = MinOuter; }

	// Fill nested COMPLETE squares OUTSIDE-IN as ONE CONTINUOUS evenly-spaced outline per square: the 4
	// corners are real members (the FIRST member of each side sits exactly on its corner), and each side
	// marches from its corner toward the next, spaced by footprint, ending ONE gap short of the next
	// corner (which the next side's corner member fills). The spacing is continuous THROUGH the corners —
	// no corner gap, a strict square. Centre stays EMPTY; only the innermost square is ever partial.
	// Corners (clockwise): (H,H) → (H,−H) → (−H,−H) → (−H,H), each side marching d ∈ [0,2H] from its corner.
	Layout.Positions.SetNum(N);
	int32 Idx = 0;
	FFixedPoint H = OuterHalfSide;
	while (Idx < N)
	{
		const bool bFloorRing = (H - RadialGap) < MinHalfSide;
		const FFixedPoint Perim = H * FFixedPoint::FromInt(8);

		// Gather this square's members (tight: Σ diameter ≤ 8H; the first always fits; a floor square
		// takes all that remain).
		const int32 Start = Idx;
		FFixedPoint Used = FFixedPoint::Zero;
		while (Idx < N)
		{
			const FFixedPoint D = EffR[Idx] * FFixedPoint::Two;
			if (Idx > Start && !bFloorRing && (Used + D) > Perim) { break; }
			Used = Used + D;
			++Idx;
		}
		const int32 K = Idx - Start; // members on this square

		// Split K across the 4 sides as evenly as possible; each side's FIRST member is its corner.
		const int32 Base = K / 4;
		const int32 Rem  = K % 4;

		const FFixedPoint CornerX[4] = { H,                    H,                    FFixedPoint::Zero - H, FFixedPoint::Zero - H };
		const FFixedPoint CornerY[4] = { H,                    FFixedPoint::Zero - H, FFixedPoint::Zero - H, H                    };
		const FFixedPoint DirX[4]    = { FFixedPoint::Zero,    FFixedPoint::Zero - FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::One };
		const FFixedPoint DirY[4]    = { FFixedPoint::Zero - FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero };
		const FFixedPoint SideLen    = H * FFixedPoint::Two;

		int32 Cursor = Start;
		for (int32 s = 0; s < 4; ++s)
		{
			const int32 c = Base + (s < Rem ? 1 : 0);
			if (c <= 0) { continue; }

			// Even slack so this side's `c` members (corner-inclusive) fill [0, SideLen]: `c` gaps total
			// (member→member, last member→next corner), so the last member ends one gap short of the next
			// corner. Footprint-tight when there's no slack.
			FFixedPoint Tight = FFixedPoint::Zero;
			for (int32 i = 0; i < c; ++i)
			{
				const FFixedPoint Ri  = EffR[Cursor + i];
				const FFixedPoint Rnx = (i + 1 < c) ? EffR[Cursor + i + 1] : Ri; // phantom next corner ≈ same size
				Tight = Tight + (Ri + Rnx);
			}
			FFixedPoint Slack = FFixedPoint::Zero;
			if (SideLen > Tight) { Slack = (SideLen - Tight) / FFixedPoint::FromInt(c); }

			FFixedPoint d = FFixedPoint::Zero;
			for (int32 i = 0; i < c; ++i)
			{
				const int32 mi = Cursor + i;
				const FFixedPoint lx = CornerX[s] + DirX[s] * d;
				const FFixedPoint ly = CornerY[s] + DirY[s] * d;
				const FFixedVector WorldOffset = Facing.RotateVector(FFixedVector(lx, ly, FFixedPoint::Zero));
				Layout.Positions[mi] = ProjectToNavigable(World, Center + WorldOffset, Center);

				const FFixedPoint Ri  = EffR[mi];
				const FFixedPoint Rnx = (i + 1 < c) ? EffR[mi + 1] : Ri;
				d = d + (Ri + Rnx) + Slack; // advance: footprint gap + even slack
			}
			Cursor += c;
		}

		if (bFloorRing) { break; }
		H = H - RadialGap;
	}
	return Layout;
}
