/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinExtentsHelpers.h
 * @brief   Static helpers for reading FSeinExtentsComponent off an actor class
 *          without instantiating the actor.
 *
 *          The targeter (Phase 3+) needs the extents of a building before
 *          spawning it — to size the placement preview, run client-side
 *          footprint validation, and feed the server-side placement resolver.
 *          The canonical source is the actor BP's USeinEntityBridgeComponent
 *          ComponentData array; these helpers walk that path with no runtime
 *          spawn.
 *
 *          Pure header — implementation kept inline so consumers don't need
 *          to link a cpp. Caller must hold a hard ref to keep the class
 *          loaded for the duration of the returned pointer.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Actor/SeinEntityBridgeComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Types/Transform.h"
#include "Types/Vector.h"

namespace SeinExtentsHelpers
{
	/**
	 * Returns the FSeinExtentsComponent payload authored on `ActorClass`'s entity
	 * bridge ComponentData, or null if none. The pointer's lifetime is bound
	 * to the CDO — safe to read while the class is loaded; copy into a local
	 * FSeinExtentsComponent if the caller needs to outlive the load lifetime.
	 */
	inline const FSeinExtentsComponent* GetExtentsFromActorClass(TSubclassOf<AActor> ActorClass)
	{
		if (!ActorClass) return nullptr;
		// GetActorClassDefaultComponents walks BOTH native + SCS templates in
		// stable order — required for "what would this BP look like at spawn"
		// CDO inspection. FindComponentByClass on a CDO misses SCS-added
		// components.
		TArray<const USeinEntityBridgeComponent*> Bridges;
		AActor::GetActorClassDefaultComponents<USeinEntityBridgeComponent>(ActorClass, Bridges);
		for (const USeinEntityBridgeComponent* Bridge : Bridges)
		{
			if (!Bridge) continue;
			if (const FSeinExtentsComponent* Found = Bridge->FindAuthoredData<FSeinExtentsComponent>())
			{
				return Found;
			}
		}
		return nullptr;
	}

	/**
	 * Returns the first FSeinExtentsShape on the actor class, or null if the
	 * class has no extents component or the component has no shapes. Most
	 * convenient for buildings with one body shape (the typical case);
	 * compound entities should iterate the full Shapes array instead.
	 */
	inline const FSeinExtentsShape* GetPrimaryExtentsShape(TSubclassOf<AActor> ActorClass)
	{
		const FSeinExtentsComponent* Data = GetExtentsFromActorClass(ActorClass);
		if (!Data || Data->Shapes.Num() == 0) return nullptr;
		return &Data->Shapes[0];
	}

	/**
	 * Conservative bounding-radius for a single extents shape (top-down,
	 * planar). Capsule → Radius; Box → diagonal of half-extents (corner
	 * distance). The bounding radius is the smallest distance from the
	 * shape's center where any approach direction is guaranteed outside
	 * the shape — using it for "stand at the edge" lookups gives a
	 * direction-agnostic standoff that's always clear of the footprint.
	 *
	 * Trade-off: for axis-aligned approaches to a long, thin box the
	 * bounding radius overshoots the actual edge by up to (diag - shorterHalf).
	 * Most RTS buildings are roughly square so the overshoot is small.
	 * If the user really wants pixel-perfect "closest point on oriented
	 * box surface" math, that's the point-to-OBB clamp pattern (rotate
	 * approach into shape's local, clamp by half-extents, rotate back) —
	 * left out of V1 for simplicity. */
	inline FFixedPoint BoundingRadius(const FSeinExtentsShape& Shape)
	{
		if (Shape.Shape == ESeinExtentsShape::Capsule)
		{
			return Shape.Radius;
		}
		// Box: diagonal of half-extents = sqrt(HX² + HY²). Reuse FFixedVector::Size
		// rather than calling Sqrt directly.
		return FFixedVector(Shape.HalfExtentX, Shape.HalfExtentY, FFixedPoint::Zero).Size();
	}

	/**
	 * Planar bounding radius of a whole collider, measured from the ENTITY
	 * centre (not each shape's own centre): max over shapes of
	 * (|Shape.LocalOffset.XY| + BoundingRadius(Shape)). Accounts for shapes
	 * offset away from the entity origin, so a compound body's true reach is
	 * covered. Used by the collision broadphase (footprint cell-stamping +
	 * query radius) and the collision resolver (neighbour query + mass), which
	 * must agree on collider size. Returns 0 for an empty shape set. */
	inline FFixedPoint GetColliderBoundingRadius(const FSeinExtentsComponent& Extents)
	{
		FFixedPoint MaxRadius = FFixedPoint::Zero;
		for (const FSeinExtentsShape& Shape : Extents.Shapes)
		{
			FFixedVector OffsetXY = Shape.LocalOffset;
			OffsetXY.Z = FFixedPoint::Zero;
			const FFixedPoint R = OffsetXY.Size() + BoundingRadius(Shape);
			if (R > MaxRadius) MaxRadius = R;
		}
		return MaxRadius;
	}

	/**
	 * Compute a "stand here" point just outside a target entity's footprint,
	 * on the surface CLOSEST to `ApproachFrom`. Used by AutoMoveThen so a
	 * unit walking up to a building stops at the building's edge instead of
	 * its center — matching standard "build/repair/attack on the
	 * footprint perimeter" semantics.
	 *
	 * Geometry (top-down, planar — Z preserved from EntityTransform):
	 *   - Capsule (radially symmetric): EntityCenter + Direction × Radius +
	 *     Buffer along approach direction.
	 *   - Box (oriented): closest point on the box surface to ApproachFrom
	 *     (point-to-OBB clamp in box-local space) + Buffer along the
	 *     surface-normal-toward-approach direction.
	 *
	 * The Box implementation is FACE-AWARE — for a face-on approach the
	 * standoff sits a `Buffer` distance off the face (much closer to the
	 * box than a bounding-circle would put it). For corner approaches the
	 * standoff sits at the corner. Either way, the resulting standoff
	 * point's distance from the entity center equals the box's radius IN
	 * THE APPROACH DIRECTION + Buffer — meaning a Build ability with
	 * MaxRange > face-radius + Buffer will reach it from any face-on
	 * approach, even if MaxRange < diagonal.
	 *
	 * Edge cases:
	 *   - `Shape` null → returns EntityCenter (caller should range-check).
	 *   - Approach point at entity center → returns EntityCenter (degenerate).
	 *   - Approach point INSIDE the box → returns the approach point pushed
	 *     outward along (ApproachFrom - EntityCenter).
	 *
	 * V1 simplification: ignores `Shape.LocalOffset` and `Shape.YawOffsetDegrees`
	 * — assumes shape sits at the entity's transform with no extra rotation.
	 * Compound entities or off-axis turrets may want a more careful version
	 * that composes the shape's local pose; deferred. */
	inline FFixedVector ComputeStandoffPoint(
		const FSeinExtentsShape* Shape,
		const FFixedTransform& EntityTransform,
		const FFixedVector& ApproachFrom,
		FFixedPoint Buffer = FFixedPoint::FromInt(50))
	{
		const FFixedVector EntityCenter = EntityTransform.GetLocation();
		if (!Shape) return EntityCenter;

		// --- Capsule path (radially symmetric — closed-form) ---
		if (Shape->Shape == ESeinExtentsShape::Capsule)
		{
			FFixedVector ToApproach = ApproachFrom - EntityCenter;
			ToApproach.Z = FFixedPoint::Zero;
			const FFixedPoint Distance = ToApproach.Size();
			if (Distance <= FFixedPoint::Zero) return EntityCenter;
			const FFixedVector Direction = ToApproach / Distance;
			return EntityCenter + Direction * (Shape->Radius + Buffer);
		}

		// --- Box path (oriented; closest-point-on-OBB) ---
		// 1. Translate ApproachFrom into the box's local frame (entity transform).
		const FFixedVector LocalApproach = EntityTransform.InverseTransformPosition(ApproachFrom);

		// 2. Clamp local XY to box half-extents → closest local point on the box.
		//    (For external approaches the clamp pushes onto the surface; for
		//     internal approaches the clamp returns the approach point unchanged
		//     — handled by the post-rotate fallback below.)
		const FFixedPoint HX = Shape->HalfExtentX;
		const FFixedPoint HY = Shape->HalfExtentY;
		FFixedPoint LX = LocalApproach.X;
		if (LX >  HX) LX =  HX;
		if (LX < -HX) LX = -HX;
		FFixedPoint LY = LocalApproach.Y;
		if (LY >  HY) LY =  HY;
		if (LY < -HY) LY = -HY;
		const FFixedVector LocalClosest(LX, LY, FFixedPoint::Zero);

		// 3. Back to world.
		const FFixedVector WorldClosest = EntityTransform.TransformPosition(LocalClosest);

		// 4. Push the standoff outward along the surface-normal-toward-approach
		//    direction. If ApproachFrom is outside, this is just the unit vector
		//    from WorldClosest to ApproachFrom. If ApproachFrom is inside the
		//    box (degenerate; clamps were no-ops), fall back to the radial-from-
		//    center direction.
		FFixedVector OutwardDir = ApproachFrom - WorldClosest;
		OutwardDir.Z = FFixedPoint::Zero;
		FFixedPoint OutwardDist = OutwardDir.Size();
		if (OutwardDist <= FFixedPoint::Zero)
		{
			FFixedVector Radial = ApproachFrom - EntityCenter;
			Radial.Z = FFixedPoint::Zero;
			OutwardDist = Radial.Size();
			if (OutwardDist <= FFixedPoint::Zero) return WorldClosest;
			OutwardDir = Radial;
		}
		return WorldClosest + (OutwardDir / OutwardDist) * Buffer;
	}
}
