/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverGeometry.h
 * @brief   Shared inline geometry helpers for cover system queries.
 *
 *          Three consumers share the same primitives: the combat damage
 *          helper (`USeinCoverBPFL::SeinGetCoverDirection`) computes
 *          per-shot cover direction; the cover-aware snap resolvers use
 *          the same direction primitive to partition slots by the cursor's
 *          side of cover before selection-wide assignment. Kept inline in a
 *          header to share without introducing a cpp-level dependency
 *          between the BPFL, the default cover impl, and the resolvers.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinExtentsComponent.h"
#include "Core/SeinEntityHandle.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/SeinCoverTypes.h"
#include "Types/Vector.h"
#include "Types/Box.h"
#include "Types/Sphere.h"
#include "Types/Capsule.h"
#include "Math/GeometryQueries.h"

namespace SeinCoverGeometry
{
	/** Compute the nearest surface point on a single SeinExtents shape, in
	 *  world space. Query point is also in world space; the shape's local
	 *  parameters (LocalOffset, YawOffsetDegrees) are applied on top of the
	 *  entity's world transform.
	 *
	 *  Box: clamps the query point into the shape's local AABB (after the
	 *  shape's yaw + entity rotation), then transforms back. Inside the box,
	 *  the clamped point equals the input → caller treats that as "interior"
	 *  (direction degenerate).
	 *
	 *  Capsule: projects onto the shape's axis (Z from LocalOffset.Z up to
	 *  LocalOffset.Z + Height), then pushes radially outward to the cylinder
	 *  surface. */
	inline FFixedVector NearestPointOnShape(
		const FFixedVector& EntityLocation,
		const FFixedQuaternion& EntityRotation,
		const FSeinExtentsShape& Shape,
		const FFixedVector& WorldPoint)
	{
		// Shape's world-space pivot = entity location + entity-rotated local offset.
		// Shape's world-space rotation = entity rotation × shape yaw rotation.
		const FFixedVector ShapePivot = EntityLocation + EntityRotation.RotateVector(Shape.LocalOffset);

		const FFixedPoint YawRad = Shape.YawOffsetDegrees * FFixedPoint::DegToRad;
		const FFixedQuaternion ShapeYawRot = FFixedQuaternion::FromAxisAndAngle(
			FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::One), YawRad);
		const FFixedQuaternion ShapeWorldRot = EntityRotation * ShapeYawRot;

		// Bring the query point into the shape's local frame.
		const FFixedVector RelToPivot = WorldPoint - ShapePivot;
		const FFixedVector LocalQuery = ShapeWorldRot.Inverse().RotateVector(RelToPivot);

		FFixedVector LocalNearest = LocalQuery;

		switch (Shape.Shape)
		{
			case ESeinExtentsShape::Box:
			{
				const FFixedPoint HX = Shape.HalfExtentX;
				const FFixedPoint HY = Shape.HalfExtentY;
				LocalNearest.X = SeinMath::Clamp(LocalQuery.X, -HX, HX);
				LocalNearest.Y = SeinMath::Clamp(LocalQuery.Y, -HY, HY);
				LocalNearest.Z = SeinMath::Clamp(LocalQuery.Z, FFixedPoint::Zero, Shape.Height);
				break;
			}
			case ESeinExtentsShape::Capsule:
			{
				const FFixedPoint ClampedZ = SeinMath::Clamp(LocalQuery.Z, FFixedPoint::Zero, Shape.Height);
				const FFixedPoint RadialX = LocalQuery.X;
				const FFixedPoint RadialY = LocalQuery.Y;
				const FFixedPoint RadialDistSq = RadialX * RadialX + RadialY * RadialY;
				if (RadialDistSq <= FFixedPoint::Zero)
				{
					LocalNearest = FFixedVector(Shape.Radius, FFixedPoint::Zero, ClampedZ);
				}
				else
				{
					const FFixedPoint RadialDist = SeinMath::Sqrt(RadialDistSq);
					const FFixedPoint Scale = Shape.Radius / RadialDist;
					LocalNearest = FFixedVector(RadialX * Scale, RadialY * Scale, ClampedZ);
				}
				break;
			}
			default:
				break;
		}

		return ShapePivot + ShapeWorldRot.RotateVector(LocalNearest);
	}

	/** True if the circle (world `Center`, radius `R`) overlaps the entity's solid
	 *  body — any of its extents shapes. Poses each shape into its local frame and
	 *  defers the actual intersection to the core `SeinGeometry` primitives; the
	 *  cover layer only does the posing. Cover slot selection uses this to reject a
	 *  slot whose footprint would clip a wall, even when the slot's nav cell
	 *  rasterized as passable. Pure fixed-point; deterministic. */
	inline bool CircleOverlapsExtents(
		const FSeinExtentsComponent* Extents,
		const FFixedVector& EntityLocation,
		const FFixedQuaternion& EntityRotation,
		const FFixedVector& Center,
		const FFixedPoint& R)
	{
		if (!Extents || Extents->Shapes.Num() == 0) return false;

		for (const FSeinExtentsShape& Shape : Extents->Shapes)
		{
			// Pose the query circle into the shape's local frame (pivot + yaw +
			// entity rotation), same transform as NearestPointOnShape. Distance is
			// rotation-invariant, so the overlap test runs entirely in local space.
			const FFixedVector ShapePivot = EntityLocation + EntityRotation.RotateVector(Shape.LocalOffset);
			const FFixedPoint YawRad = Shape.YawOffsetDegrees * FFixedPoint::DegToRad;
			const FFixedQuaternion ShapeYawRot = FFixedQuaternion::FromAxisAndAngle(
				FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::One), YawRad);
			const FFixedQuaternion ShapeWorldRot = EntityRotation * ShapeYawRot;
			const FFixedSphere QueryCircle(ShapeWorldRot.Inverse().RotateVector(Center - ShapePivot), R);

			switch (Shape.Shape)
			{
				case ESeinExtentsShape::Box:
				{
					const FFixedBox LocalBox(
						FFixedVector(-Shape.HalfExtentX, -Shape.HalfExtentY, FFixedPoint::Zero),
						FFixedVector( Shape.HalfExtentX,  Shape.HalfExtentY, Shape.Height));
					if (SeinGeometry::BoxIntersectsSphere(LocalBox, QueryCircle)) return true;
					break;
				}
				case ESeinExtentsShape::Capsule:
				{
					const FFixedCapsule LocalCapsule(
						FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, FFixedPoint::Zero),
						FFixedVector(FFixedPoint::Zero, FFixedPoint::Zero, Shape.Height),
						Shape.Radius);
					if (SeinGeometry::CapsuleIntersectsSphere(LocalCapsule, QueryCircle)) return true;
					break;
				}
				default:
					break;
			}
		}
		return false;
	}

	/** True if the circle (world `Center`, radius `R`) overlaps the cover quality
	 *  zone (`FSeinCoverArea`) posed at the entity transform. Box areas pose into the
	 *  entity's local frame; sphere areas are rotation-invariant. Geometry math is
	 *  the core `SeinGeometry` set. Used to assign a slot the BEST quality of every
	 *  cover area it touches (a light-wall slot inside a heavy area reads heavy). */
	inline bool CircleOverlapsCoverArea(
		const FSeinCoverArea& Area,
		const FFixedVector& EntityLocation,
		const FFixedQuaternion& EntityRotation,
		const FFixedVector& Center,
		const FFixedPoint& R)
	{
		switch (Area.Shape)
		{
			case ESeinCoverAreaShape::Box:
			{
				// Area box is centered on the entity origin (±LocalExtents per axis).
				const FFixedVector LocalCenter = EntityRotation.Inverse().RotateVector(Center - EntityLocation);
				const FFixedBox LocalBox = FFixedBox::FromCenterAndExtent(FFixedVector::ZeroVector, Area.LocalExtents);
				return SeinGeometry::BoxIntersectsSphere(LocalBox, FFixedSphere(LocalCenter, R));
			}
			case ESeinCoverAreaShape::Sphere:
			{
				return SeinGeometry::SphereIntersectsSphere(
					FFixedSphere(EntityLocation, Area.LocalExtents.X),
					FFixedSphere(Center, R));
			}
			default:
				return false;
		}
	}

	/** Inner implementation: compute outward direction using pre-fetched
	 *  extents data + entity transform. Avoids the per-call GetComponent /
	 *  GetEntity lookups when the caller already has them (e.g. inside a
	 *  per-slot loop where every iteration shares one provider).
	 *
	 *  See `OutwardFromExtents` for the semantics + return contract; this
	 *  helper is just the math without the data plumbing. */
	inline FFixedVector OutwardFromExtentsCached(
		const FSeinExtentsComponent* Extents,
		const FFixedVector& EntityLocation,
		const FFixedQuaternion& EntityRotation,
		const FFixedVector& WorldPoint)
	{
		if (!Extents || Extents->Shapes.Num() == 0) return FFixedVector::ZeroVector;

		// Walk all shapes; pick the closest nearest-point across the union.
		FFixedVector BestPoint = FFixedVector::ZeroVector;
		FFixedPoint  BestDistSq = FFixedPoint::MaxValue;
		bool bAny = false;
		for (const FSeinExtentsShape& Shape : Extents->Shapes)
		{
			const FFixedVector P = NearestPointOnShape(EntityLocation, EntityRotation, Shape, WorldPoint);
			const FFixedPoint  DistSq = FFixedVector::DistSquared(WorldPoint, P);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestPoint = P;
				bAny = true;
			}
		}
		if (!bAny) return FFixedVector::ZeroVector;

		const FFixedVector Outward = WorldPoint - BestPoint;
		const FFixedPoint LenSq = Outward.SizeSquared();
		if (LenSq <= FFixedPoint::Zero) return FFixedVector::ZeroVector;
		const FFixedPoint Len = SeinMath::Sqrt(LenSq);
		return FFixedVector(Outward.X / Len, Outward.Y / Len, Outward.Z / Len);
	}

	/** Compute the outward direction from the cover provider's SeinExtents
	 *  body to a query world point. Returns a UNIT vector pointing from the
	 *  nearest surface point on the entity's combined extents shapes to the
	 *  query point.
	 *
	 *  Returns zero vector when:
	 *    - Provider has no FSeinExtentsComponent component (no body to query)
	 *    - Provider's Shapes array is empty
	 *    - Query point coincides with the surface point (degenerate; nearly
	 *      zero in practice, caller can treat as "fully covered" or whatever
	 *      its damage formula prefers).
	 *
	 *  Convenience wrapper around `OutwardFromExtentsCached` for one-shot
	 *  callers; inside per-slot or per-cell loops, prefer the cached variant
	 *  with pre-fetched Extents + entity transform to avoid redundant
	 *  WorldSub->GetComponent / GetEntity lookups. */
	inline FFixedVector OutwardFromExtents(
		USeinWorldSubsystem* WorldSub,
		FSeinEntityHandle ProviderHandle,
		const FFixedVector& WorldPoint)
	{
		if (!WorldSub) return FFixedVector::ZeroVector;
		const FSeinExtentsComponent* Extents = WorldSub->GetComponent<FSeinExtentsComponent>(ProviderHandle);
		if (!Extents) return FFixedVector::ZeroVector;
		const FSeinEntity* Entity = WorldSub->GetEntity(ProviderHandle);
		if (!Entity) return FFixedVector::ZeroVector;
		return OutwardFromExtentsCached(Extents,
			Entity->Transform.GetLocation(),
			Entity->Transform.GetQuaternionRotation(),
			WorldPoint);
	}

	/** Partition a list of slot candidates into "preferred side" (same outward
	 *  side as the cursor relative to the provider's SeinExtents body) and
	 *  "wrong side" (opposite). The shared assignment planner maximizes total
	 *  coverage first, then minimizes wrong-side use and total distance. This
	 *  keeps side preference from reducing assignment cardinality.
	 *
	 *  Slots from non-directional providers (foxholes — `WorldProtectedFromDirection`
	 *  is zero) go into the preferred set unconditionally: omni cover has no
	 *  meaningful "wrong side". Cursor outward is cached per provider — O(1)
	 *  extra cost per provider regardless of slot count.
	 *
	 *  Output arrays are filled with indices into the input `Slots` array;
	 *  callers preserve the original slot ordering as the final deterministic
	 *  assignment tie-break. */
	inline void PartitionSlotsByCursorSide(
		USeinWorldSubsystem* WorldSub,
		const TArray<FSeinCoverSlotCandidate>& Slots,
		const FFixedVector& CursorPosition,
		TArray<int32>& OutPreferredIndices,
		TArray<int32>& OutWrongSideIndices)
	{
		OutPreferredIndices.Reset();
		OutWrongSideIndices.Reset();
		OutPreferredIndices.Reserve(Slots.Num());

		if (!WorldSub)
		{
			// No world sub — can't compute outward. Treat everything as preferred.
			for (int32 i = 0; i < Slots.Num(); ++i) OutPreferredIndices.Add(i);
			return;
		}

		// Cache cursor outward per provider — many slots share one provider.
		TMap<FSeinEntityHandle, FFixedVector> CursorOutwardByProvider;
		CursorOutwardByProvider.Reserve(8);

		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const FSeinCoverSlotCandidate& Slot = Slots[i];

			// Non-directional provider (slot WPFD is zero) → no meaningful side
			// distinction. Always preferred (treat as omni-OK).
			if (Slot.WorldProtectedFromDirection.IsNearlyZero())
			{
				OutPreferredIndices.Add(i);
				continue;
			}

			const FFixedVector* Cached = CursorOutwardByProvider.Find(Slot.ProviderHandle);
			FFixedVector CursorOutward;
			if (Cached)
			{
				CursorOutward = *Cached;
			}
			else
			{
				CursorOutward = OutwardFromExtents(WorldSub, Slot.ProviderHandle, CursorPosition);
				CursorOutwardByProvider.Add(Slot.ProviderHandle, CursorOutward);
			}

			// If we couldn't compute the cursor's outward direction (provider
			// has no SeinExtents, or cursor is exactly on a surface point),
			// fall back to "always preferred" — we can't make a side decision,
			// so don't filter anything out.
			if (CursorOutward.IsNearlyZero())
			{
				OutPreferredIndices.Add(i);
				continue;
			}

			// Both vectors point AWAY from the cover body. Same side ⇒
			// dot ≥ 0; opposite side ⇒ dot < 0. Zero is treated as same-side
			// (perpendicular case is ambiguous; lean toward inclusion).
			const FFixedPoint SideDot = FFixedVector::DotProduct(CursorOutward, Slot.WorldProtectedFromDirection);
			if (SideDot >= FFixedPoint::Zero)
			{
				OutPreferredIndices.Add(i);
			}
			else
			{
				OutWrongSideIndices.Add(i);
			}
		}
	}
}
