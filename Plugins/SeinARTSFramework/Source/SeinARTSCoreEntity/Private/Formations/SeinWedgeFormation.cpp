/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWedgeFormation.cpp
 * @brief   Footprint-aware hollow chevron; large selections fan into nested chevrons (layer count
 *          AUTOMATIC from packing), apexes stepping back so they nest — keeps the wedge compact
 *          instead of one ballooning V. Units pack along the arms by their REAL footprints (biggest
 *          at the tip, variant spacing). A drag sizes the arms; a plain click is one tight chevron.
 */

#include "Formations/SeinWedgeFormation.h"
#include "Math/MathLib.h"

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

	// Footprint radii drive spacing + preview dot sizing.
	GatherFootprintRadii(World, Members, Layout.Radii);

	// Facing + center: a drag faces FORWARD over the guide line (the NEGATED perpendicular) so the
	// body trails BEHIND it; a plain click faces the move target.
	const FFixedVector DragPerp = DragFacingDir(Target.GuidePoints);
	const bool bDrag = !DragPerp.IsNearlyZero();
	const FFixedVector Center = bDrag
		? (Target.GuidePoints[0] + Target.GuidePoints.Last()) / FFixedPoint::Two
		: Target.Anchor;
	Layout.Facing = bDrag
		? FacingFromDirection(FFixedVector::ZeroVector - DragPerp)
		: ComputeFormationFacing(Target.CurrentCentroid, Target.CurrentFacing, Target.Anchor);
	const FFixedQuaternion Facing = Layout.Facing;

	// A 1-unit wedge is degenerate — the member stands at the tip.
	if (N == 1)
	{
		Layout.Positions.Add(ProjectToNavigable(World, Center, Center));
		return Layout;
	}

	// Effective footprint radius per member (= its real radius). VARIANT spacing — units are placed by
	// their OWN footprints along the arms, never by a single uniform "largest" gap; InterUnitSpacing is
	// added once per gap.
	FFixedPoint MaxEffR = FFixedPoint::Zero, SumEffR = FFixedPoint::Zero;
	TArray<FFixedPoint> EffR; EffR.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		EffR[i] = Layout.Radii[i];
		if (EffR[i] < FFixedPoint::Zero) { EffR[i] = FFixedPoint::Zero; }
		SumEffR = SumEffR + EffR[i];
		if (EffR[i] > MaxEffR) { MaxEffR = EffR[i]; }
	}
	if (MaxEffR <= FFixedPoint::Zero) { MaxEffR = FFixedPoint::FromInt(25); }

	// Arm directions from the half-angle. Local +X is forward; arms run back-and-out at ±theta from the
	// back (−X) axis. Unit-length (cos²+sin²=1).
	FFixedPoint Theta = HalfAngleDegrees * FFixedPoint::TwoPi / FFixedPoint::FromInt(360);
	const FFixedPoint C = SeinMath::Cos(Theta);
	FFixedPoint S = SeinMath::Sin(Theta);
	const FFixedPoint MinS = FFixedPoint::One / FFixedPoint::FromInt(100); // guard the apex step's /S
	if (S < MinS) { S = MinS; }

	// Outer arm length A. A DRAG sets it (half the drag — honor the drag fully); a plain CLICK sizes ONE
	// tight chevron whose two arms (outline 2A) hold everyone: 2A = Σ diameter = 2·SumEffR → A = SumEffR.
	FFixedVector DragVec = FFixedVector::ZeroVector;
	if (bDrag) { DragVec = Target.GuidePoints.Last() - Target.GuidePoints[0]; DragVec.Z = FFixedPoint::Zero; }
	FFixedPoint A = bDrag ? (DragVec.Size() / FFixedPoint::Two) : SumEffR;
	if (A < MaxEffR) { A = MaxEffR; } // floor: at least seat the tip unit

	// Layer count is AUTOMATIC from packing (no threshold step-graph): how many chevrons of outline 2A it
	// takes to seat N at their real footprints. A click is one tight chevron; a shorter drag → more
	// nested chevrons, a longer drag → fewer (expand consumes inner chevrons). Capacity-by-Σdiameter is
	// exact here because an arm is straight (chord == along-arm distance).
	const FFixedPoint ArmOutline = A * FFixedPoint::Two; // both arms of one chevron
	int32 Layers = 1;
	{
		FFixedPoint Used = FFixedPoint::Zero; int32 Cap = 0;
		for (int32 i = 0; i < N; ++i)
		{
			const FFixedPoint D = EffR[i] * FFixedPoint::Two + InterUnitSpacing;
			if (Cap > 0 && (Used + D) > ArmOutline) { break; }
			Used = Used + D; ++Cap;
		}
		if (Cap < 1) { Cap = 1; }
		Layers = FMath::Clamp((N + Cap - 1) / Cap, 1, N);
	}

	// Each chevron's apex steps BACK along the axis so the chevrons NEST with a perpendicular gap of one
	// diameter between adjacent arms (apex step = diameter / sin theta). Layer 0's apex is the tip at the
	// Center; deeper layers sit behind it → an arrowhead with depth.
	const FFixedPoint ApexStep = (MaxEffR * FFixedPoint::Two) / S;

	// Distribute members across the chevrons biggest-FIRST so the largest seat at the front chevron and
	// its apex (the tip), filling each chevron's 2A outline before stepping back to the next.
	const TArray<int32> MemBySize = SortIndicesByRadiusDesc(EffR); // biggest first
	TArray<TArray<int32>> ChevMembers; ChevMembers.SetNum(Layers);
	{
		int32 k = 0; FFixedPoint Used = FFixedPoint::Zero;
		for (int32 j = 0; j < N; ++j)
		{
			const int32 mi = MemBySize[j];
			const FFixedPoint D = EffR[mi] * FFixedPoint::Two + InterUnitSpacing;
			if (k < Layers - 1 && Used > FFixedPoint::Zero && (Used + D) > ArmOutline) { ++k; Used = FFixedPoint::Zero; }
			ChevMembers[k].Add(mi);
			Used = Used + D;
		}
	}

	// Place each chevron: the biggest member sits at the apex; the rest alternate onto the left / right
	// arm (whichever is currently shorter, to balance), packed outward by their REAL footprints — a tank
	// eats more arm than a rifleman. The apex unit (the biggest) clears the first arm units, and the
	// SeparatePositions net downstream guarantees no residual overlap.
	Layout.Positions.SetNum(N);
	for (int32 k = 0; k < Layers; ++k)
	{
		const TArray<int32>& Mem = ChevMembers[k];
		const int32 M = Mem.Num();
		if (M == 0) { continue; }
		const FFixedPoint ApexX = FFixedPoint::Zero - ApexStep * FFixedPoint::FromInt(k);

		// Apex unit (biggest of this chevron) on the axis.
		Layout.Positions[Mem[0]] = ProjectToNavigable(World, Center + Facing.RotateVector(FFixedVector(ApexX, FFixedPoint::Zero, FFixedPoint::Zero)), Center);
		FFixedPoint DistL = FFixedPoint::Zero, DistR = FFixedPoint::Zero; // last placed unit's centre distance per arm
		FFixedPoint LastRL = EffR[Mem[0]], LastRR = EffR[Mem[0]];         // apex unit is the shared "previous"

		for (int32 j = 1; j < M; ++j)
		{
			const int32 mi = Mem[j];
			const FFixedPoint r = EffR[mi];
			const bool bLeft = (DistL <= DistR);              // feed the shorter arm to keep them balanced
			FFixedPoint& Dist  = bLeft ? DistL  : DistR;
			FFixedPoint& LastR = bLeft ? LastRL : LastRR;
			Dist  = Dist + LastR + r + InterUnitSpacing;      // touch the previous unit on this arm
			LastR = r;
			const FFixedPoint DirY = bLeft ? (FFixedPoint::Zero - S) : S;
			const FFixedPoint Lx = ApexX + (FFixedPoint::Zero - C) * Dist; // arm runs backward (−X)
			const FFixedPoint Ly = DirY * Dist;
			Layout.Positions[mi] = ProjectToNavigable(World, Center + Facing.RotateVector(FFixedVector(Lx, Ly, FFixedPoint::Zero)), Center);
		}
	}
	return Layout;
}
