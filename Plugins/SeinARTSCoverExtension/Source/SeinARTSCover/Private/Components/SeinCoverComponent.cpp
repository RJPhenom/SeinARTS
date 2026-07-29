/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverComponent.cpp
 *
 * Slot generator for FSeinCoverComponent. Lives in the sim module (not
 * SeinARTSCoverEditor) because the math is deterministic (FFixedPoint) and
 * the struct is sim-side — the editor module only owns the button + details
 * customization that drives this method.
 *
 * Two modes:
 *   - Edge: walks the perimeter of the caller-supplied wall-body Box shape
 *     (sibling FSeinExtentsComponent's first Box). Slots sit OUTSIDE the
 *     body by `GenerateSlotInsetUU` — inside the cover Area, outside the
 *     wall. Transforms the body's LocalOffset + YawOffsetDegrees so a
 *     rotated wall produces correctly-rotated slot positions.
 *   - Area: fills the interior of `Area` (Box or Sphere) with concentric
 *     inset rings. Designer authors a foxhole / crater via Area=Box or
 *     Sphere; slots sit INSIDE that volume. No sibling needed.
 *
 * Adapted (post-Phase-5) from the pre-refactor AC's GenerateSlotsAlongEdge.
 * Edge mode reads sibling extents geometry the same way the legacy AC did
 * — the caller (cover details panel) resolves the sibling and passes the
 * shape pointer; this keeps the sim struct ignorant of the entity bridge.
 */

#include "Components/SeinCoverComponent.h"
#include "Components/SeinExtentsComponent.h"   // FSeinExtentsShape for Edge mode
#include "Math/MathLib.h"
#include "Types/FixedPoint.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCoverGen, Log, All);

namespace SeinCoverGenLocal
{
	/** Transform a shape-local 2D point (X, Y) into actor-local space using
	 *  the shape's pre-computed yaw cosine/sine + LocalOffset translation.
	 *  Z is taken from the offset (slots live on the shape's base plane). */
	static FFixedVector ShapeLocalToActorLocal(
		FFixedPoint LocalX, FFixedPoint LocalY,
		FFixedPoint CosY, FFixedPoint SinY,
		const FFixedVector& ShapeOffset)
	{
		return FFixedVector(
			CosY * LocalX - SinY * LocalY + ShapeOffset.X,
			SinY * LocalX + CosY * LocalY + ShapeOffset.Y,
			ShapeOffset.Z);
	}

	/** Map a perimeter distance (with wrap-around) into a 2D point on a closed
	 *  rectangle perimeter of half-extents HX/HY. Edges walked CCW starting
	 *  at the (+X, -Y) corner: front (+X side, walking +Y) → right (+Y side,
	 *  walking -X) → back (-X side, walking -Y) → left (-Y side, walking +X).
	 *
	 *  Wrap-tolerant via modular subtraction — accepts any Dist, including
	 *  negative or > perimeter (jittered or pre-wrapped distances). */
	static void PerimeterDistToShapeLocal(
		FFixedPoint Dist, FFixedPoint HX, FFixedPoint HY,
		FFixedPoint& OutX, FFixedPoint& OutY)
	{
		const FFixedPoint TwoHX = HX * FFixedPoint::Two;
		const FFixedPoint TwoHY = HY * FFixedPoint::Two;
		const FFixedPoint Perim = TwoHX + TwoHY + TwoHX + TwoHY; // = 4*(HX+HY) split per edge

		// Wrap Dist into [0, Perim). while-loops handle negative and over-perim
		// equally; in practice jitter is bounded so iteration count is ≤ 1.
		FFixedPoint D = Dist;
		while (D < FFixedPoint::Zero) D += Perim;
		while (D >= Perim) D -= Perim;

		if (D < TwoHY)
		{
			// Front edge: start (HX, -HY), walk +Y.
			OutX = HX;
			OutY = -HY + D;
			return;
		}
		D -= TwoHY;
		if (D < TwoHX)
		{
			// Right edge: start (HX, HY), walk -X.
			OutX = HX - D;
			OutY = HY;
			return;
		}
		D -= TwoHX;
		if (D < TwoHY)
		{
			// Back edge: start (-HX, HY), walk -Y.
			OutX = -HX;
			OutY = HY - D;
			return;
		}
		D -= TwoHY;
		// Left edge: start (-HX, -HY), walk +X.
		OutX = -HX + D;
		OutY = -HY;
	}

	/** Generate slots along a closed rectangle perimeter at the given
	 *  half-extents, optionally transformed by a shape pose. Used by both
	 *  Edge mode (outer perimeter at body-extents + Inset, transformed by
	 *  the body's LocalOffset/YawOffset) and Area-Box ring mode (each ring's
	 *  perimeter, no extra transform — area is already in actor space).
	 *
	 *  When `bScatter` is true, each slot's perimeter distance is jittered
	 *  within ±0.4 × Spacing — slots remain on the edge but with uneven
	 *  spacing (some pairs closer, some farther). 0.4 (not 0.5) keeps each
	 *  slot from crossing into its neighbor's lane. Editor-time RNG is fine
	 *  here (FMath::FRandRange) — the output is serialized into the .uasset
	 *  as FFixedVector positions, so cross-platform determinism is preserved
	 *  at runtime regardless of which editor RNG produced the layout.
	 *
	 *  When `bApplyShapePose` is false, slots are emitted at axis-aligned
	 *  shape-local positions (Area-Box mode). When true, each slot is rotated
	 *  by CosY/SinY and translated by ShapeOffset before being added. */
	static void EmitPerimeterSlots(FFixedPoint HX, FFixedPoint HY,
		int32 SlotCount, TArray<FFixedVector>& OutSlots,
		bool bScatter = false,
		bool bApplyShapePose = false,
		FFixedPoint CosY = FFixedPoint::One, FFixedPoint SinY = FFixedPoint::Zero,
		const FFixedVector& ShapeOffset = FFixedVector::ZeroVector)
	{
		if (SlotCount <= 0 || HX <= FFixedPoint::Zero || HY <= FFixedPoint::Zero) return;

		const FFixedPoint Perim = (HX + HY) * FFixedPoint::FromInt(4);
		const FFixedPoint Spacing = Perim / FFixedPoint::FromInt(SlotCount);
		const FFixedPoint HalfSpacing = Spacing / FFixedPoint::Two;
		const float JitterMaxUU = bScatter ? Spacing.ToFloat() * 0.4f : 0.0f;

		for (int32 i = 0; i < SlotCount; ++i)
		{
			FFixedPoint Dist = HalfSpacing + Spacing * FFixedPoint::FromInt(i);
			if (bScatter)
			{
				Dist = Dist + FFixedPoint::FromFloat(FMath::FRandRange(-JitterMaxUU, JitterMaxUU));
			}

			FFixedPoint LocalX, LocalY;
			PerimeterDistToShapeLocal(Dist, HX, HY, LocalX, LocalY);

			if (bApplyShapePose)
			{
				OutSlots.Add(ShapeLocalToActorLocal(LocalX, LocalY, CosY, SinY, ShapeOffset));
			}
			else
			{
				OutSlots.Add(FFixedVector(LocalX, LocalY, FFixedPoint::Zero));
			}
		}
	}

	/** Helper for area-scatter modes: is `Candidate` at least `MinDist` (cell-
	 *  center distance) away from every entry already in OutSlots? Comparison
	 *  done in float because the loop runs at editor authoring time; the
	 *  result is then stored as FFixedVector (cross-platform deterministic). */
	static bool IsFarEnoughFromExisting(
		const FFixedVector& Candidate, const TArray<FFixedVector>& OutSlots, float MinDistSq)
	{
		for (const FFixedVector& Existing : OutSlots)
		{
			const float DX = (Candidate.X - Existing.X).ToFloat();
			const float DY = (Candidate.Y - Existing.Y).ToFloat();
			if (DX * DX + DY * DY < MinDistSq)
			{
				return false;
			}
		}
		return true;
	}

	/** Scatter random points inside an axis-aligned box (half-extents HX/HY)
	 *  with min-distance + edge-margin constraints. Rejection sampling — keeps
	 *  drawing candidates and discarding ones that violate the constraints
	 *  until SlotCount are placed or MaxAttempts is exhausted. */
	static void EmitAreaBoxScatter(FFixedPoint HX, FFixedPoint HY,
		FFixedPoint Margin, FFixedPoint MinSpacing,
		int32 SlotCount, TArray<FFixedVector>& OutSlots)
	{
		if (SlotCount <= 0 || HX <= FFixedPoint::Zero || HY <= FFixedPoint::Zero) return;

		// Clamp sampling region by Margin from the edge. If Margin would
		// collapse the region, shrink it so we can still sample SOMETHING
		// rather than degenerate to a point — designer error is logged at
		// the call site if results are sparse.
		const FFixedPoint MinSampleHX = HX - Margin;
		const FFixedPoint MinSampleHY = HY - Margin;
		const FFixedPoint SampleHX = MinSampleHX > FFixedPoint::Zero ? MinSampleHX : HX;
		const FFixedPoint SampleHY = MinSampleHY > FFixedPoint::Zero ? MinSampleHY : HY;

		const float MinSpacingFloat = MinSpacing.ToFloat();
		const float MinSpacingSq = MinSpacingFloat * MinSpacingFloat;
		const int32 MaxAttempts = SlotCount * 100;

		OutSlots.Reserve(OutSlots.Num() + SlotCount);
		int32 Attempts = 0;
		while (OutSlots.Num() < SlotCount && Attempts++ < MaxAttempts)
		{
			const float RX = FMath::FRandRange(-SampleHX.ToFloat(), SampleHX.ToFloat());
			const float RY = FMath::FRandRange(-SampleHY.ToFloat(), SampleHY.ToFloat());
			const FFixedVector Candidate(
				FFixedPoint::FromFloat(RX), FFixedPoint::FromFloat(RY), FFixedPoint::Zero);

			if (IsFarEnoughFromExisting(Candidate, OutSlots, MinSpacingSq))
			{
				OutSlots.Add(Candidate);
			}
		}

		if (OutSlots.Num() < SlotCount)
		{
			UE_LOG(LogSeinCoverGen, Warning,
				TEXT("[EmitAreaBoxScatter] Could only place %d/%d slots in %d attempts — area too small for spacing (HX=%.1f, HY=%.1f, Margin=%.1f, MinSpacing=%.1f). Increase area, decrease count, or decrease GenerateSlotInsetUU."),
				OutSlots.Num(), SlotCount, Attempts,
				HX.ToFloat(), HY.ToFloat(), Margin.ToFloat(), MinSpacingFloat);
		}
	}

	/** Scatter random points inside a sphere (radius R, treated as 2D disc in
	 *  XY since slots are planar) with min-distance + edge-margin constraints.
	 *  Rejection sampling in a bounding box, then disc-distance filter. */
	static void EmitAreaSphereScatter(FFixedPoint Radius,
		FFixedPoint Margin, FFixedPoint MinSpacing,
		int32 SlotCount, TArray<FFixedVector>& OutSlots)
	{
		if (SlotCount <= 0 || Radius <= FFixedPoint::Zero) return;

		// Shrink sampling radius by Margin. Same defensive clamp as Box.
		const FFixedPoint MinSampleR = Radius - Margin;
		const FFixedPoint SampleR = MinSampleR > FFixedPoint::Zero ? MinSampleR : Radius;
		const float SampleRFloat = SampleR.ToFloat();
		const float SampleRSq = SampleRFloat * SampleRFloat;

		const float MinSpacingFloat = MinSpacing.ToFloat();
		const float MinSpacingSq = MinSpacingFloat * MinSpacingFloat;
		const int32 MaxAttempts = SlotCount * 100;

		OutSlots.Reserve(OutSlots.Num() + SlotCount);
		int32 Attempts = 0;
		while (OutSlots.Num() < SlotCount && Attempts++ < MaxAttempts)
		{
			const float RX = FMath::FRandRange(-SampleRFloat, SampleRFloat);
			const float RY = FMath::FRandRange(-SampleRFloat, SampleRFloat);
			if (RX * RX + RY * RY > SampleRSq)
			{
				continue;       // outside the disc — reject
			}

			const FFixedVector Candidate(
				FFixedPoint::FromFloat(RX), FFixedPoint::FromFloat(RY), FFixedPoint::Zero);

			if (IsFarEnoughFromExisting(Candidate, OutSlots, MinSpacingSq))
			{
				OutSlots.Add(Candidate);
			}
		}

		if (OutSlots.Num() < SlotCount)
		{
			UE_LOG(LogSeinCoverGen, Warning,
				TEXT("[EmitAreaSphereScatter] Could only place %d/%d slots in %d attempts — disc too small for spacing (R=%.1f, Margin=%.1f, MinSpacing=%.1f). Increase radius, decrease count, or decrease GenerateSlotInsetUU."),
				OutSlots.Num(), SlotCount, Attempts,
				Radius.ToFloat(), Margin.ToFloat(), MinSpacingFloat);
		}
	}
}

#if WITH_EDITORONLY_DATA
void FSeinCoverComponent::GenerateSlots(const FSeinExtentsShape* OptionalEdgeShape)
{
	UE_LOG(LogSeinCoverGen, Log,
		TEXT("[GenerateSlots] Mode=%s, Scatter=%s, Count=%d, Inset=%.1f, Area.Shape=%s, Area.LocalExtents=(%.1f,%.1f,%.1f), EdgeShape=%s"),
		GenerateMode == ESeinCoverGenerateMode::Edge ? TEXT("Edge") : TEXT("Area"),
		bScatterSlots ? TEXT("yes") : TEXT("no"),
		GenerateSlotCount, GenerateSlotInsetUU,
		Area.Shape == ESeinCoverAreaShape::None   ? TEXT("None") :
		Area.Shape == ESeinCoverAreaShape::Box    ? TEXT("Box")  : TEXT("Sphere"),
		Area.LocalExtents.X.ToFloat(),
		Area.LocalExtents.Y.ToFloat(),
		Area.LocalExtents.Z.ToFloat(),
		OptionalEdgeShape ? TEXT("provided") : TEXT("<null>"));

	Slots.Reset();

	if (GenerateSlotCount <= 0)
	{
		UE_LOG(LogSeinCoverGen, Warning,
			TEXT("[GenerateSlots] GenerateSlotCount=%d, bailing"), GenerateSlotCount);
		return;
	}

	const FFixedPoint Inset = FFixedPoint::FromFloat(GenerateSlotInsetUU);

	if (GenerateMode == ESeinCoverGenerateMode::Edge)
	{
		// Edge mode wraps the sibling Extents body. Caller resolves the
		// FSeinExtentsComponent's first Box shape and passes it in — this
		// struct stays ignorant of the entity bridge. Bail with a warning
		// when no shape was passed or the passed shape isn't a Box (Edge
		// cover assumes a rectangular wall body).
		if (!OptionalEdgeShape)
		{
			UE_LOG(LogSeinCoverGen, Warning,
				TEXT("[GenerateSlots] Edge mode requires a sibling FSeinExtentsComponent "
				     "with a Box shape on the same entity. Add an Extents component (Box) "
				     "and retry. (The cover details panel resolves and passes it.)"));
			return;
		}
		if (OptionalEdgeShape->Shape != ESeinExtentsShape::Box)
		{
			UE_LOG(LogSeinCoverGen, Warning,
				TEXT("[GenerateSlots] Edge mode requires the sibling Extents shape to be Box, got Capsule. "
				     "Switch to Box (or use Area mode with a sphere foxhole)."));
			return;
		}

		// Inset OUTWARD from the body's half-extents — slots sit beyond the
		// wall surface by `Inset`, on the protected side. The body's local
		// pose (LocalOffset + YawOffsetDegrees) is applied per-slot so a
		// rotated wall body produces correctly-rotated slot positions.
		// Scatter (when enabled) jitters the perimeter spacing; the slots
		// themselves stay anchored on the OuterHX/OuterHY perimeter.
		const FFixedPoint OuterHX = OptionalEdgeShape->HalfExtentX + Inset;
		const FFixedPoint OuterHY = OptionalEdgeShape->HalfExtentY + Inset;

		const FFixedPoint YawRad = OptionalEdgeShape->YawOffsetDegrees * FFixedPoint::DegToRad;
		const FFixedPoint CosY = SeinMath::Cos(YawRad);
		const FFixedPoint SinY = SeinMath::Sin(YawRad);

		Slots.Reserve(GenerateSlotCount);
		SeinCoverGenLocal::EmitPerimeterSlots(
			OuterHX, OuterHY, GenerateSlotCount, Slots,
			/*bScatter*/ bScatterSlots,
			/*bApplyShapePose*/ true, CosY, SinY, OptionalEdgeShape->LocalOffset);

		// Edge mode → directional. Combat math reads bIsDirectional to decide
		// whether to call SeinGetCoverDirection per shot.
		bIsDirectional = true;
		return;
	}

	// Area mode — fill interior with concentric inset rings (Box) or circles
	// (Sphere) when scatter is off; random scattered points (no overlaps,
	// inset from edge) when scatter is on. Set bIsDirectional = false: area
	// cover is omnidirectional (foxhole, crater).
	bIsDirectional = false;

	switch (Area.Shape)
	{
		case ESeinCoverAreaShape::Box:
		{
			const FFixedPoint HX = Area.LocalExtents.X;
			const FFixedPoint HY = Area.LocalExtents.Y;
			if (HX <= FFixedPoint::Zero || HY <= FFixedPoint::Zero)
			{
				UE_LOG(LogSeinCoverGen, Warning,
					TEXT("[GenerateSlots] Box area has zero/negative extents (HX=%.1f, HY=%.1f) — cannot generate."),
					HX.ToFloat(), HY.ToFloat());
				return;
			}

			if (bScatterSlots)
			{
				// Random points inside the box, inset from the edge by
				// `Inset`, with `Inset` as the minimum slot-to-slot center
				// distance. Designer can tune Inset for tighter or looser
				// packing.
				SeinCoverGenLocal::EmitAreaBoxScatter(
					HX, HY, /*Margin*/ Inset, /*MinSpacing*/ Inset,
					GenerateSlotCount, Slots);
				break;
			}

			// Concentric inset rectangle rings. Outer perimeter sets the
			// reference; inner rings get slot-counts weighted by perimeter
			// ratio so density stays roughly uniform. Stops on degenerate
			// ring OR budget exhaustion.
			const FFixedPoint OuterPerim = (HX + HY) * FFixedPoint::FromInt(4);
			int32 SlotsRemaining = GenerateSlotCount;
			FFixedPoint RingOffset = Inset;

			Slots.Reserve(GenerateSlotCount);

			int32 Safety = 0;
			while (SlotsRemaining > 0 && Safety++ < 64)
			{
				const FFixedPoint RingHX = HX - RingOffset;
				const FFixedPoint RingHY = HY - RingOffset;
				if (RingHX <= FFixedPoint::Zero || RingHY <= FFixedPoint::Zero) break;

				const FFixedPoint RingPerim = (RingHX + RingHY) * FFixedPoint::FromInt(4);
				const FFixedPoint RatioFP = RingPerim / OuterPerim;
				int32 RingSlotCount = FMath::Max(1,
					FMath::CeilToInt(RatioFP.ToFloat() * static_cast<float>(GenerateSlotCount)));
				RingSlotCount = FMath::Min(RingSlotCount, SlotsRemaining);

				SeinCoverGenLocal::EmitPerimeterSlots(RingHX, RingHY, RingSlotCount, Slots);
				SlotsRemaining -= RingSlotCount;
				RingOffset += Inset;
			}
			break;
		}
		case ESeinCoverAreaShape::Sphere:
		{
			// LocalExtents.X is the radius (Y/Z ignored — matches the query
			// convention in SeinCoverDefault::QueryCoverAt).
			const FFixedPoint SphereRadius = Area.LocalExtents.X;
			if (SphereRadius <= FFixedPoint::Zero)
			{
				UE_LOG(LogSeinCoverGen, Warning,
					TEXT("[GenerateSlots] Sphere area has zero/negative radius (%.1f) — cannot generate."),
					SphereRadius.ToFloat());
				return;
			}

			if (bScatterSlots)
			{
				SeinCoverGenLocal::EmitAreaSphereScatter(
					SphereRadius, /*Margin*/ Inset, /*MinSpacing*/ Inset,
					GenerateSlotCount, Slots);
				break;
			}

			// Concentric circles inset by `Inset` per ring. Slot count per
			// ring weighted by circumference. Center slot emitted when ring
			// budget allows. Stops on degenerate ring OR budget exhaustion.
			const FFixedPoint OuterCircum = SphereRadius * FFixedPoint::TwoPi;
			int32 SlotsRemaining = GenerateSlotCount;
			FFixedPoint RingRadius = SphereRadius - Inset;
			bool bEmittedCenter = false;

			Slots.Reserve(GenerateSlotCount);

			int32 Safety = 0;
			while (SlotsRemaining > 0 && Safety++ < 64)
			{
				if (RingRadius <= FFixedPoint::Zero)
				{
					// Out of ring radius — emit one center slot if budget allows.
					if (SlotsRemaining > 0 && !bEmittedCenter)
					{
						Slots.Add(FFixedVector::ZeroVector);
						--SlotsRemaining;
						bEmittedCenter = true;
					}
					break;
				}

				const FFixedPoint RingCircum = RingRadius * FFixedPoint::TwoPi;
				const FFixedPoint RatioFP = RingCircum / OuterCircum;
				int32 RingSlotCount = FMath::Max(1,
					FMath::CeilToInt(RatioFP.ToFloat() * static_cast<float>(GenerateSlotCount)));
				RingSlotCount = FMath::Min(RingSlotCount, SlotsRemaining);

				for (int32 i = 0; i < RingSlotCount; ++i)
				{
					const FFixedPoint Angle = (FFixedPoint::TwoPi * FFixedPoint::FromInt(i))
						/ FFixedPoint::FromInt(RingSlotCount);
					const FFixedPoint CosA = SeinMath::Cos(Angle);
					const FFixedPoint SinA = SeinMath::Sin(Angle);
					Slots.Add(FFixedVector(CosA * RingRadius, SinA * RingRadius, FFixedPoint::Zero));
				}
				SlotsRemaining -= RingSlotCount;
				RingRadius -= Inset;
			}
			break;
		}
		case ESeinCoverAreaShape::None:
		default:
			UE_LOG(LogSeinCoverGen, Warning,
				TEXT("[GenerateSlots] Area mode requires Area.Shape != None. Configure Area "
				     "(Box or Sphere) with non-zero extents and try again."));
			break;
	}
}
#endif
