/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCursorTrace.h
 * @author       RJ Macklem
 * @created      5 Sep 2026
 * @latest       5 Sep 2026
 * @brief        Presentation-side cursor queries against live Sein Extents.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"

class UWorld;
struct FSeinExtentsPayload;

namespace SeinCursorTrace
{
	/** Intersect authored boxes/capsules at the displayed actor pose. Actor scale,
	 *  mesh bounds, physics bodies, and collision responses do not affect picking.
	 *  Direction must be normalized; Distance is in world units. */
	SEINARTSFRAMEWORK_API bool IntersectExtents(const FSeinExtentsPayload& Extents,
		const FTransform& ActorTransform, const FVector& Origin, const FVector& Direction,
		double MaxDistance, double& Distance);

	/** Pick the nearest visible, live registered entity with intersecting extents.
	 *  Ownership/selection policy belongs to the caller; equal-depth ties use handles.
	 *  No Unreal collision query participates in entity picking. */
	SEINARTSFRAMEWORK_API bool TraceEntities(UWorld& World, const FVector& Origin,
		const FVector& Direction, double MaxDistance, FHitResult& OutHit);

	/** Resolve world geometry only, ignoring every Sein actor. This fallback supplies
	 *  ground positions when the entity query misses; its channel cannot pick a unit. */
	SEINARTSFRAMEWORK_API bool TraceEnvironment(UWorld& World, const FVector& Origin,
		const FVector& Direction, double MaxDistance, ECollisionChannel Channel, FHitResult& OutHit);
}
