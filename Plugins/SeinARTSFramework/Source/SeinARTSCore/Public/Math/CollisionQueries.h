/**
 * SeinARTS Framework
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		CollisionQueries.h
 * @date:		6/2/2026
 * @author:		RJ Macklem
 * @brief:		Deterministic fixed-point PENETRATION queries for the collision
 * 				layer. Where GeometryQueries.h answers boolean "do these
 * 				intersect?" / raycast questions, this file answers the harder
 * 				"by how much do they overlap, and which way do I push to separate
 * 				them?" — i.e. it returns a Minimum Translation Vector (MTV):
 * 				a contact normal + penetration depth.
 *
 * 				PLANAR BY DESIGN. SeinARTS is a top-down RTS; the collision
 * 				resolver only ever pushes bodies apart in the XY ground plane
 * 				(Z is owned by movement/ground-snap). So every query here works
 * 				in 2D: a vertical Capsule collapses to a disc (its axis is a
 * 				single point from above) and an oriented Box collapses to an
 * 				oriented 2D rectangle (OBB). Vertical separation, if it matters,
 * 				is a cheap 1D range test the caller runs first (RangesOverlap).
 *
 * 				CORE STAYS COMPONENT-AGNOSTIC. These functions take raw geometric
 * 				params (centers, unit axes, half-extents, radii) — they know
 * 				nothing about FSeinExtentsShape or entities. The collision
 * 				resolver (SeinARTSCoreEntity) builds the params from an entity's
 * 				Extents shapes and dispatches to the right query.
 *
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Math/MathLib.h"

/**
 * Penetration / MTV query utilities for the collision layer. All functions are
 * deterministic, fixed-point, and operate in the XY plane (Z ignored).
 *
 * NORMAL CONVENTION (uniform across every query): the returned `Normal` points
 * from shape A toward shape B and is unit length in XY. To separate the pair,
 * move B along `+Normal` and A along `-Normal` (split however the resolver's
 * mass model dictates). `Depth` is the overlap distance along that normal and
 * is always >= 0 when `bHit` is true. `Normal.Z` is always zero.
 */
namespace SeinCollision
{
	/**
	 * Result of a narrow-phase penetration query.
	 *
	 *  - bHit   : true iff the two shapes overlap (touching-exactly counts as
	 *             NOT hit — zero penetration is nothing to resolve).
	 *  - Normal : unit separation axis in the XY plane, oriented A -> B.
	 *  - Depth  : penetration depth along Normal (>= 0). Multiply by Normal to
	 *             get the full MTV that would separate the pair if applied
	 *             entirely to B (or its negation applied to A).
	 */
	struct FSeinContact2D
	{
		bool         bHit   = false;
		FFixedVector Normal = FFixedVector::ZeroVector;
		FFixedPoint  Depth  = FFixedPoint::Zero;
	};

	/**
	 * 1D range-overlap test, for the optional vertical (Z) early-out. Returns
	 * true if [MinA, MaxA] and [MinB, MaxB] overlap at all. The resolver runs
	 * this on the two shapes' Z spans before the planar query so a unit can't
	 * "collide" with the roof of a tunnel it's standing under, or a ground unit
	 * with a high-flying one. Inclusive at the touching boundary — a shared edge
	 * counts as overlapping (matches the conservative "treat ambiguous as
	 * touching" stance; planar penetration will be ~zero anyway).
	 */
	FORCEINLINE bool RangesOverlap(FFixedPoint MinA, FFixedPoint MaxA, FFixedPoint MinB, FFixedPoint MaxB)
	{
		return MinA <= MaxB && MinB <= MaxA;
	}

	/**
	 * Disc vs disc (two top-down capsules / radial footprints).
	 * Normal points from A's center toward B's center.
	 *
	 * Degenerate coincident-centers case (DistSq ~ 0) biases the push to +X so
	 * the resolution stays deterministic — real motion almost never produces
	 * exactly coincident centers, but lockstep must not branch on luck.
	 */
	FORCEINLINE FSeinContact2D DiscVsDisc(
		const FFixedVector& CenterA, FFixedPoint RadiusA,
		const FFixedVector& CenterB, FFixedPoint RadiusB)
	{
		FSeinContact2D Contact;

		FFixedVector Delta = CenterB - CenterA;
		Delta.Z = FFixedPoint::Zero;                       // planar
		const FFixedPoint DistSq = Delta.SizeSquared();
		const FFixedPoint Sum    = RadiusA + RadiusB;
		const FFixedPoint SumSq  = Sum * Sum;
		if (DistSq >= SumSq) return Contact;               // separated (>= => touching is not a hit)

		if (DistSq <= FFixedPoint::Epsilon)
		{
			// Coincident centers — direction undefined; bias +X deterministically.
			Contact.bHit   = true;
			Contact.Normal = FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
			Contact.Depth  = Sum;                          // fully overlapped
			return Contact;
		}

		const FFixedPoint Dist = SeinMath::Sqrt(DistSq);
		Contact.bHit   = true;
		Contact.Normal = Delta / Dist;                     // A -> B
		Contact.Depth  = Sum - Dist;
		return Contact;
	}

	/**
	 * Disc (A) vs oriented box (B). `AxisX` / `AxisY` MUST be unit length and
	 * lie in the XY plane (they're the box's local X/Y directions in world
	 * space); `HalfX` / `HalfY` are the box half-extents along those axes.
	 * Normal points from the disc toward the box.
	 *
	 * Two regimes:
	 *   - Disc center OUTSIDE the box footprint: separation runs along the line
	 *     from the closest point on the box surface to the disc center.
	 *   - Disc center INSIDE the box footprint: there's no "closest surface
	 *     point" direction, so we pop the disc out the nearest face (the axis
	 *     with the least remaining penetration). Depth includes the full radius
	 *     plus the inside distance to that face.
	 */
	FORCEINLINE FSeinContact2D DiscVsOBB(
		const FFixedVector& DiscCenter, FFixedPoint DiscRadius,
		const FFixedVector& BoxCenter,  const FFixedVector& AxisX, const FFixedVector& AxisY,
		FFixedPoint HalfX, FFixedPoint HalfY)
	{
		FSeinContact2D Contact;

		// Disc center in the box's local frame (project onto unit axes).
		FFixedVector Delta = DiscCenter - BoxCenter;
		Delta.Z = FFixedPoint::Zero;
		const FFixedPoint LocalX = FFixedVector::DotProduct(Delta, AxisX);
		const FFixedPoint LocalY = FFixedVector::DotProduct(Delta, AxisY);

		const FFixedPoint ClampedX = SeinMath::Clamp(LocalX, -HalfX, HalfX);
		const FFixedPoint ClampedY = SeinMath::Clamp(LocalY, -HalfY, HalfY);

		// If clamping changed nothing on both axes the center sits inside the box.
		const bool bInside = (LocalX == ClampedX) && (LocalY == ClampedY);

		if (!bInside)
		{
			// Closest point on the box surface, back in world space.
			const FFixedVector Closest = BoxCenter + AxisX * ClampedX + AxisY * ClampedY;
			FFixedVector Out = DiscCenter - Closest;        // box surface -> disc center (B -> A)
			Out.Z = FFixedPoint::Zero;
			const FFixedPoint DistSq = Out.SizeSquared();
			if (DistSq >= DiscRadius * DiscRadius) return Contact;   // separated

			Contact.bHit  = true;
			if (DistSq <= FFixedPoint::Epsilon)
			{
				// Disc center exactly on the surface — derive a stable normal
				// from the box-center-to-disc direction; final fallback +X.
				FFixedVector Radial = DiscCenter - BoxCenter;
				Radial.Z = FFixedPoint::Zero;
				FFixedVector N = FFixedVector::GetSafeNormal(Radial);   // B -> A direction
				if (N.IsZero()) N = FFixedVector(FFixedPoint::One, FFixedPoint::Zero, FFixedPoint::Zero);
				Contact.Normal = -N;                          // A -> B
				Contact.Depth  = DiscRadius;
			}
			else
			{
				const FFixedPoint Dist = SeinMath::Sqrt(DistSq);
				Contact.Normal = (-Out) / Dist;               // negate (B->A) to get A->B
				Contact.Depth  = DiscRadius - Dist;
			}
			return Contact;
		}

		// Inside: eject through the nearest face.
		const FFixedPoint PenX = HalfX - SeinMath::Abs(LocalX);   // inward distance to an X face
		const FFixedPoint PenY = HalfY - SeinMath::Abs(LocalY);   // inward distance to a Y face

		Contact.bHit = true;
		if (PenX <= PenY)
		{
			const FFixedPoint Sign = (LocalX >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
			const FFixedVector FaceOut = AxisX * Sign;            // outward toward disc side (B -> A)
			Contact.Normal = -FaceOut;                            // A -> B
			Contact.Depth  = DiscRadius + PenX;
		}
		else
		{
			const FFixedPoint Sign = (LocalY >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
			const FFixedVector FaceOut = AxisY * Sign;
			Contact.Normal = -FaceOut;
			Contact.Depth  = DiscRadius + PenY;
		}
		return Contact;
	}

	/**
	 * Oriented box (A) vs oriented box (B), 2D SAT. All four axis vectors MUST
	 * be unit length and planar. In 2D the complete set of potential separating
	 * axes is exactly the four box face normals (A's X/Y and B's X/Y) — unlike
	 * 3D there are no edge-cross axes to test, because box edges are parallel to
	 * the face normals. The MTV is the axis of least overlap.
	 *
	 * Normal points from A toward B (the least-overlap axis, sign-flipped to
	 * agree with the A->B center direction).
	 */
	FORCEINLINE FSeinContact2D OBBVsOBB(
		const FFixedVector& CenterA, const FFixedVector& AxisXA, const FFixedVector& AxisYA, FFixedPoint HalfXA, FFixedPoint HalfYA,
		const FFixedVector& CenterB, const FFixedVector& AxisXB, const FFixedVector& AxisYB, FFixedPoint HalfXB, FFixedPoint HalfYB)
	{
		FSeinContact2D Contact;

		FFixedVector T = CenterB - CenterA;                 // A -> B
		T.Z = FFixedPoint::Zero;

		FFixedPoint  MinOverlap = FFixedPoint::MaxValue;
		FFixedVector BestAxis   = FFixedVector::ZeroVector;

		// Project both boxes onto a candidate unit axis L; return false the
		// instant a separating axis is found (no overlap → no collision).
		const auto TestAxis = [&](const FFixedVector& L) -> bool
		{
			const FFixedPoint RadiusA =
				HalfXA * SeinMath::Abs(FFixedVector::DotProduct(AxisXA, L)) +
				HalfYA * SeinMath::Abs(FFixedVector::DotProduct(AxisYA, L));
			const FFixedPoint RadiusB =
				HalfXB * SeinMath::Abs(FFixedVector::DotProduct(AxisXB, L)) +
				HalfYB * SeinMath::Abs(FFixedVector::DotProduct(AxisYB, L));
			const FFixedPoint CenterDist = SeinMath::Abs(FFixedVector::DotProduct(T, L));
			const FFixedPoint Overlap    = RadiusA + RadiusB - CenterDist;
			if (Overlap <= FFixedPoint::Zero) return false;  // separating axis
			if (Overlap < MinOverlap)
			{
				MinOverlap = Overlap;
				BestAxis   = L;
			}
			return true;
		};

		if (!TestAxis(AxisXA)) return Contact;
		if (!TestAxis(AxisYA)) return Contact;
		if (!TestAxis(AxisXB)) return Contact;
		if (!TestAxis(AxisYB)) return Contact;

		Contact.bHit  = true;
		Contact.Depth = MinOverlap;
		// Orient the chosen axis to point A -> B.
		Contact.Normal = (FFixedVector::DotProduct(T, BestAxis) < FFixedPoint::Zero) ? -BestAxis : BestAxis;
		return Contact;
	}
}
