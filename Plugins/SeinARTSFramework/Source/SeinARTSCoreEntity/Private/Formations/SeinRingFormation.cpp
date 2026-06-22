/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinRingFormation.cpp
 * @brief   Tight concentric-ring perimeter about the anchor — members pack as densely as their
 *          footprints allow around the ring(s), centre left EMPTY. A drag sets the outer radius;
 *          extra members spill into tighter inner rings. Deterministic fixed-point trig.
 */

#include "Formations/SeinRingFormation.h"
#include "Math/MathLib.h"

USeinRingFormation::USeinRingFormation()
{
	FacingMode = ESeinFormationFacing::RadialOutward;
}

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

	// A 1-unit ring is degenerate — the member just stands at the center.
	if (N == 1)
	{
		Layout.Positions.Add(ProjectToNavigable(World, Center, Center));
		return Layout;
	}

	const FFixedQuaternion Facing = Layout.Facing;

	// Effective footprint radius per member = real radius + half the optional InterUnitSpacing margin
	// (neighbours then touch at exactly r_i + r_j + spacing). The biggest effective DIAMETER sets the
	// radial gap between concentric rings AND the minimum ring radius — so the innermost ring keeps a
	// hollow centre and the chord/asin geometry stays well-conditioned (r/s <= 0.5 there).
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
	FFixedPoint RadialGap = MaxEffR * FFixedPoint::Two;            // adjacent rings clear by a full diameter
	if (RadialGap <= FFixedPoint::Zero) { RadialGap = FFixedPoint::FromInt(50); }
	const FFixedPoint MinRingRadius = RadialGap;                   // floor: hollow centre + r/s <= 0.5

	const FFixedPoint TwoPi  = FFixedPoint::TwoPi;
	const FFixedPoint ArgCap = FFixedPoint::FromInt(99) / FFixedPoint::FromInt(100); // asin domain guard

	// Per-unit angular WIDTH on a ring of radius s: w = 2*asin(r / s). Two neighbours touch when their
	// centre-to-centre CHORD equals r_i + r_j, and Sum(w) <= 2*pi is the (slightly conservative,
	// asin-convex) no-overlap capacity — packs tight by chord, NEVER by arc (the old overlap bug).
	auto AngularWidth = [&](FFixedPoint EffRadius, FFixedPoint S) -> FFixedPoint
	{
		if (S <= FFixedPoint::Zero) { return TwoPi; }
		FFixedPoint Arg = EffRadius / S;
		if (Arg > ArgCap) { Arg = ArgCap; }
		if (Arg < FFixedPoint::Zero) { Arg = FFixedPoint::Zero; }
		return FFixedPoint::Two * SeinMath::Asin(Arg);
	};

	// FitsAll(OuterR): do concentric TIGHT rings from OuterR down to MinRingRadius hold all N? Mirrors the
	// fill below (same member order, same per-ring chord capacity). Used to size the compact CLICK and to
	// floor a hard shrink.
	auto FitsAll = [&](FFixedPoint OuterR) -> bool
	{
		FFixedPoint s = OuterR; int32 u = 0;
		while (u < N && s >= MinRingRadius)
		{
			const int32 St = u; FFixedPoint SumW = FFixedPoint::Zero;
			while (u < N)
			{
				const FFixedPoint W = AngularWidth(EffR[u], s);
				if (u > St && (SumW + W) > TwoPi) { break; }
				SumW = SumW + W; ++u;
			}
			s = s - RadialGap;
		}
		return u >= N;
	};

	// MinCompact = the SMALLEST outer radius whose concentric tight rings hold everyone — the most COMPACT
	// packing (nested rings; ≥2 layers for any selection past one tight ring). A plain CLICK uses this — a
	// click is the tightest multi-ring shape, NOT one big loose ring. It is also the hard-shrink floor.
	FFixedPoint MinCompact = MinRingRadius;
	for (int32 Guard = 0; Guard < 256 && !FitsAll(MinCompact); ++Guard) { MinCompact = MinCompact + RadialGap; }

	// A DRAG sets the outer radius directly — honor the drag fully (expand uncapped: a sparse single ring
	// when N can't fill the drawn circle is accepted), but never shrink below MinCompact. A plain CLICK
	// uses MinCompact (most compact).
	FFixedPoint OuterRadius;
	if (bDrag)
	{
		FFixedVector DragVec = Target.GuidePoints.Last() - Target.GuidePoints[0]; DragVec.Z = FFixedPoint::Zero;
		OuterRadius = DragVec.Size() / FFixedPoint::Two;
		if (OuterRadius < MinCompact) { OuterRadius = MinCompact; }
	}
	else
	{
		OuterRadius = MinCompact;
	}

	// Fill concentric rings OUTSIDE-IN: the outer ring packs tight and full FIRST (the visible perimeter
	// is never loose), each inner ring steps in by RadialGap, the centre stays EMPTY. Only the innermost
	// ring is ever partial — it spreads its slack evenly around its circle. Expanding the drag grows the
	// outer ring's capacity, CONSUMING inner rings; shrinking adds rings down to the floor.
	Layout.Positions.SetNum(N);
	int32 Idx = 0;
	FFixedPoint S = OuterRadius;
	while (Idx < N)
	{
		// A floor ring has no room for another ring inside it: it absorbs ALL remaining members (the
		// accepted shrink minimum; SeparatePositions later spreads any resulting overlap).
		const bool bFloorRing = (S - RadialGap) < MinRingRadius;

		// Greedily gather this ring's members (the first always fits; otherwise stop before Sum(w) > 2pi).
		const int32 Start = Idx;
		FFixedPoint SumW = FFixedPoint::Zero;
		while (Idx < N)
		{
			const FFixedPoint W = AngularWidth(EffR[Idx], S);
			if (Idx > Start && !bFloorRing && (SumW + W) > TwoPi) { break; }
			SumW = SumW + W;
			Idx++;
		}
		const int32 Count = Idx - Start;

		// Even slack so a full ring is exactly tight and a partial (innermost) ring spreads around the
		// whole circle rather than bunching on an arc.
		FFixedPoint Slack = TwoPi - SumW; if (Slack < FFixedPoint::Zero) { Slack = FFixedPoint::Zero; }
		const FFixedPoint SlackShare = (Count > 0) ? (Slack / FFixedPoint::FromInt(Count)) : FFixedPoint::Zero;

		FFixedPoint Acc = FFixedPoint::Zero;
		for (int32 p = Start; p < Idx; ++p)
		{
			const FFixedPoint W = AngularWidth(EffR[p], S);
			const FFixedPoint Angle = Acc + W / FFixedPoint::Two + SlackShare / FFixedPoint::Two;
			const FFixedVector LocalOffset(S * SeinMath::Cos(Angle), S * SeinMath::Sin(Angle), FFixedPoint::Zero);
			const FFixedVector WorldOffset = Facing.RotateVector(LocalOffset);
			Layout.Positions[p] = ProjectToNavigable(World, Center + WorldOffset, Center);
			Acc = Acc + W + SlackShare;
		}

		if (bFloorRing) { break; } // the floor ring already consumed everything left
		S = S - RadialGap;
	}
	return Layout;
}
