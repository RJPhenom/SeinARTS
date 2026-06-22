/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquareFormation.cpp
 * @brief   Complete hollow square about the anchor — ONE continuous evenly-spaced outline with a member
 *          on every CORNER, each side marching from its corner (variant footprint spacing), centre left
 *          EMPTY. A drag sets the outer half-side; extra members spill into tighter inner squares.
 */

#include "Formations/SeinSquareFormation.h"

USeinSquareFormation::USeinSquareFormation()
{
	FacingMode = ESeinFormationFacing::RadialOutward;
}

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

	// FitsAll(OuterH): do the nested squares (perimeter 8h each) from OuterH down to MinHalfSide hold all
	// N? Mirrors the fill below. Used to size the compact CLICK and to floor a hard shrink.
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

	// MinCompact = the SMALLEST half-side whose nested squares hold everyone — the most COMPACT packing
	// (nested squares; ≥2 layers for any selection past one tight square). A plain CLICK uses this — a
	// click is the tightest multi-square shape, NOT one big loose square. It is also the hard-shrink floor.
	FFixedPoint MinCompact = MinHalfSide;
	for (int32 Guard = 0; Guard < 256 && !FitsAll(MinCompact); ++Guard) { MinCompact = MinCompact + RadialGap; }

	// A DRAG sets the half-side directly — honor the drag fully (expand uncapped), but never shrink below
	// MinCompact. A plain CLICK uses MinCompact (most compact).
	FFixedPoint OuterHalfSide;
	if (bDrag)
	{
		FFixedVector DragVec = Target.GuidePoints.Last() - Target.GuidePoints[0]; DragVec.Z = FFixedPoint::Zero;
		OuterHalfSide = DragVec.Size() / FFixedPoint::Two;
		if (OuterHalfSide < MinCompact) { OuterHalfSide = MinCompact; }
	}
	else
	{
		OuterHalfSide = MinCompact;
	}

	// Fill nested COMPLETE squares OUTSIDE-IN as ONE CONTINUOUS evenly-spaced outline per square: the 4
	// corners are real members (the FIRST member of each side sits exactly on its corner), and each side
	// marches from its corner toward the next, spaced by footprint, ending ONE gap short of the next
	// corner (which the next side's corner member fills). The spacing is continuous THROUGH the corners —
	// no corner gap, a strict square. Centre stays EMPTY; only the innermost square is ever partial.
	// Corners (clockwise): (H,H) → (H,−H) → (−H,−H) → (−H,H), each side marching d ∈ [0,2H] from its corner.
	Layout.Positions.SetNum(N);
	int32 Idx = 0;
	FFixedPoint H = OuterHalfSide;
	FFixedPoint PrevH = OuterHalfSide; // half-side of the square just placed (for the per-layer gap)
	FFixedPoint PrevMax = FFixedPoint::Zero;
	bool bFirstSquare = true;
	while (Idx < N)
	{
		const bool bFloorRing = (H - RadialGap) < MinHalfSide;
		const FFixedPoint Perim = H * FFixedPoint::FromInt(8);

		// Gather this square's members (tight: Σ diameter ≤ 8H; the first always fits; a floor square
		// takes all that remain).
		const int32 Start = Idx;
		FFixedPoint Used = FFixedPoint::Zero;
		FFixedPoint RingMax = FFixedPoint::Zero;
		while (Idx < N)
		{
			const FFixedPoint D = EffR[Idx] * FFixedPoint::Two;
			if (Idx > Start && !bFloorRing && (Used + D) > Perim) { break; }
			Used = Used + D;
			if (EffR[Idx] > RingMax) { RingMax = EffR[Idx]; }
			++Idx;
		}

		// PER-LAYER radial gap: pull this square out to clear only ITSELF plus the square directly outside
		// it (PrevMax + RingMax), not the global biggest footprint, so one big unit doesn't push every
		// inner square outward. The per-pair gap is always <= the global RadialGap, so the corrected
		// half-side is >= the gather H and the gathered members still fit. The outer square keeps
		// OuterHalfSide.
		if (!bFirstSquare)
		{
			const FFixedPoint Tightened = PrevH - (PrevMax + RingMax);
			if (Tightened > H) { H = Tightened; }
		}
		// Split this square's members into 4 CONTIGUOUS sides by FOOTPRINT (not count): fill each side
		// toward an equal footprint share so a big unit (or a squad-sized vs infantry-sized mix) never
		// overruns one side and marches its members PAST the corner onto the next edge (the "bleeding
		// edge" the old K/4 count split produced). The guard leaves >= 1 member for each remaining side
		// so all four corners stay seated for K >= 4.
		int32 SideC[4] = { 0, 0, 0, 0 };
		{
			FFixedPoint SquareSpan = FFixedPoint::Zero;
			for (int32 m = Start; m < Idx; ++m) { SquareSpan = SquareSpan + EffR[m] * FFixedPoint::Two; }
			const FFixedPoint SideTarget = SquareSpan / FFixedPoint::FromInt(4);
			int32 sSide = 0;
			FFixedPoint SideUsed = FFixedPoint::Zero;
			for (int32 m = Start; m < Idx; ++m)
			{
				++SideC[sSide];
				SideUsed = SideUsed + EffR[m] * FFixedPoint::Two;
				const int32 MembersAfter = (Idx - 1) - m;
				const int32 SidesAfter   = 3 - sSide;
				if (sSide < 3 && MembersAfter > 0 &&
					((SideUsed >= SideTarget && MembersAfter > SidesAfter) || (MembersAfter <= SidesAfter)))
				{
					++sSide;
					SideUsed = FFixedPoint::Zero;
				}
			}
		}

		const FFixedPoint CornerX[4] = { H,                    H,                    FFixedPoint::Zero - H, FFixedPoint::Zero - H };
		const FFixedPoint CornerY[4] = { H,                    FFixedPoint::Zero - H, FFixedPoint::Zero - H, H                    };
		const FFixedPoint DirX[4]    = { FFixedPoint::Zero,    FFixedPoint::Zero - FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::One };
		const FFixedPoint DirY[4]    = { FFixedPoint::Zero - FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::One, FFixedPoint::Zero };
		const FFixedPoint SideLen    = H * FFixedPoint::Two;

		int32 Cursor = Start;
		for (int32 s = 0; s < 4; ++s)
		{
			const int32 c = SideC[s];
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

		PrevH = H;
		PrevMax = RingMax;
		bFirstSquare = false;

		if (bFloorRing) { break; }
		H = H - RadialGap;
	}
	return Layout;
}
