/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPathTypes.cpp
 * @brief   Out-of-line FSeinPath methods whose math is too heavy for the header — the
 *          deterministic arc-flattening pass. Kept in a .cpp so the fixed-point trig LUT
 *          include (Math/MathLib.h) stays out of the widely-included SeinPathTypes.h.
 */

#include "SeinPathTypes.h"
#include "Math/MathLib.h"

void FSeinPath::FlattenToWaypoints(FFixedPoint MaxChordError)
{
	// Self-guard: a plain Straight route already has Waypoints == segment endpoints, so there
	// is nothing to expand. Leaving Waypoints untouched keeps this a strict no-op for every
	// shipped (all-Straight) path — the property that makes adding it a behaviorally-inert base
	// change (bit-identical A/B until a producer actually emits a typed segment).
	if (!HasTypedSegments()) return;

	const int32 SegCount = Segments.Num();
	if (SegCount == 0) return;

	TArray<FFixedVector> Flat;
	Flat.Reserve(SegCount + 1);

	// Seed with the first segment's start; thereafter each segment contributes its interior
	// samples (Arc only) plus its exact To, so shared junctions appear exactly once.
	Flat.Add(Segments[0].From);

	// Chord tolerance floor: a non-positive tolerance would blow up the sample count / divide by
	// zero, so clamp to a sane minimum world unit.
	const FFixedPoint ChordError =
		(MaxChordError > FFixedPoint::Zero) ? MaxChordError : FFixedPoint::One;

	for (int32 s = 0; s < SegCount; ++s)
	{
		const FSeinPathSegment& Seg = Segments[s];

		if (Seg.Type == ESeinPathSegmentType::Arc && Seg.Radius > FFixedPoint::Zero)
		{
			// Sample count from the sagitta bound: for a sub-arc whose chord length is c on
			// radius R, the sagitta (max deviation from the true arc) is ~ c^2 / (8R). To keep it
			// <= ChordError, bound the sample chord to sqrt(8*R*ChordError); the number of equal
			// sub-arcs is ceil(arcLength / maxChord). Uses only Sqrt (no acos) — cheap + fully
			// deterministic in fixed-point.
			//
			// FIDELITY NOTE (for the future curve producer): this is the small-angle sagitta
			// approximation. The true sagitta is R*(1 - cos(alpha/2)); the sqrt bound slightly
			// UNDER-samples for large per-sub-arc angles, so realized deviation can exceed
			// ChordError a touch there (negligible at the shipped ~5-unit tolerance where sub-arcs
			// are small). If a producer ever needs strict adherence, derive Steps from the exact
			// alpha_max = 2*Acos(1 - ChordError/R) (SeinMath::Acos is deterministic) instead.
			const FFixedPoint AbsSweep  = (Seg.SweepAngle < FFixedPoint::Zero) ? -Seg.SweepAngle : Seg.SweepAngle;
			const FFixedPoint ArcLen    = Seg.Radius * AbsSweep;
			const FFixedPoint UnderSqrt = FFixedPoint::FromInt(8) * Seg.Radius * ChordError;
			const FFixedPoint MaxChord  = SeinMath::Sqrt(UnderSqrt);

			int32 Steps = 1;
			if (MaxChord > FFixedPoint::Zero)
			{
				const FFixedPoint Ratio = ArcLen / MaxChord;
				Steps = SeinMath::Ceil(Ratio).ToInt();
			}
			if (Steps < 1)   Steps = 1;
			// Fidelity CEILING (deterministic — identical cap on every peer): a very long / very
			// tight arc that would want > 256 sub-arcs is coarsened to 256, so realized deviation
			// there exceeds ChordError. Raise it (or split the arc upstream) if a real producer
			// needs finer sampling on huge sweeps. Movement+ wheeled and tracked
			// planners emit bounded start-maneuver arcs within this ceiling.
			if (Steps > 256) Steps = 256;

			// Start angle at Center->From; step the SIGNED sweep toward To. The endpoint (i ==
			// Steps) is added exactly from Seg.To below, so only interior samples (i = 1..Steps-1)
			// are generated here — the exact endpoint avoids trig round-off at the join.
			const FFixedVector Radial  = Seg.From - Seg.Center;
			const FFixedPoint  Theta0  = SeinMath::Atan2(Radial.Y, Radial.X);
			const FFixedPoint  StepsFP = FFixedPoint::FromInt(Steps);
			const FFixedPoint  DTheta  = Seg.SweepAngle / StepsFP;

			for (int32 i = 1; i < Steps; ++i)
			{
				const FFixedPoint IFP   = FFixedPoint::FromInt(i);
				const FFixedPoint Theta = Theta0 + DTheta * IFP;
				const FFixedPoint FracZ = IFP / StepsFP;   // lerp height along the arc
				FFixedVector P;
				P.X = Seg.Center.X + Seg.Radius * SeinMath::Cos(Theta);
				P.Y = Seg.Center.Y + Seg.Radius * SeinMath::Sin(Theta);
				P.Z = Seg.From.Z + (Seg.To.Z - Seg.From.Z) * FracZ;
				Flat.Add(P);
			}
		}

		// Every kind ends at its exact To (Straight / Field / AbstractEdge / Jump collapse to
		// their endpoints; Arc closes on its exact endpoint after the interior samples).
		Flat.Add(Seg.To);
	}

	Waypoints = MoveTemp(Flat);
}
