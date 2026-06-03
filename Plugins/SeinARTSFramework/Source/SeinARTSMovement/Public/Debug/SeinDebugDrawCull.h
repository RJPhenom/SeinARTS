/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDebugDrawCull.h
 * @brief   Camera-relative cull + per-frame entity cap for debug viz.
 *
 *          The per-tick debug viz sites in this module (active-move path
 *          overlay in `SeinARTSMovementModule.cpp`, per-subclass carrot /
 *          path tangent viz) issue raw DrawDebug*
 *          calls per entity / per path cell. Cost scales linearly with unit
 *          count and map size — fine on small maps, painful at 1km² with
 *          200 units even when only ~5 are on-camera.
 *
 *          This helper exposes a settings-driven cull (distance + optional
 *          cone-frustum check) and a per-frame entity budget shared across
 *          all sites that opt in. Settings live in `USeinARTSCoreSettings`
 *          ("Debug Visualization" category).
 *
 *          Usage patterns:
 *            - Batch (gather→cull→sort→cap→draw): use `GetActiveCameraView`
 *              once + `PassesCameraCull` per candidate, sort by distance,
 *              then `TryReserveBudget` per drawn entity.
 *            - Per-call: use `ShouldDrawAndReserve` once at the top of the
 *              draw block — combines camera lookup + cull + budget reserve.
 *
 *          Shipping strip: entire helper compiles out under
 *          UE_ENABLE_DEBUG_DRAWING (matches the call sites' gating).
 *
 *          Thread-safety: intentionally NOT thread-safe. Both consumer
 *          sites run on the game thread (sim tick + TSTicker). If a future
 *          consumer runs off-GT, wrap the budget counter in atomics.
 */

#pragma once

#include "CoreMinimal.h"

#if UE_ENABLE_DEBUG_DRAWING

class UWorld;

namespace UE::SeinARTSMovement::DebugDraw
{
	/**
	 * Camera view info for debug-viz culling. Single struct so a batch
	 * gather pass can resolve the camera once and pass it to per-candidate
	 * tests, avoiding the (relatively expensive) iterator walk per call.
	 */
	struct FCameraView
	{
		FVector Location = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;  // unit vector, world-space camera +X
		float HalfFOVRadians = 0.0f;                // half horizontal FOV; 0 if unknown
		bool bValid = false;
	};

	/**
	 * Resolve the active camera viewing `World`. Tries the local player
	 * controller first (PIE / shipping), then falls back to the first
	 * editor level-viewport that's pointed at the same world (editor with
	 * no PIE).
	 *
	 * Returns an invalid view if no camera was found — consumers should
	 * fail open (skip the cull) so debug viz isn't silently lost when
	 * something unusual is going on with the world setup.
	 */
	SEINARTSMOVEMENT_API FCameraView GetActiveCameraView(UWorld* World);

	/**
	 * Distance + cone-frustum cull test against `View`. Reads
	 * `USeinARTSCoreSettings` for the distance threshold and frustum-cull
	 * enable. If `View` is invalid, returns true (fail open).
	 *
	 * Cone test is intentionally wider than the visible rectangle (1.3×
	 * tan(halfFOV)) — false-positives near the screen edge are imperceptible
	 * relative to the cost saved on units behind the camera.
	 */
	SEINARTSMOVEMENT_API bool PassesCameraCull(const FCameraView& View, const FVector& WorldLocation);

	/**
	 * Reserve a slot in the per-frame entity-cap budget. Returns true if
	 * the cap (`DebugDrawMaxEntities`) is not yet exhausted, false if it
	 * is. Counter rolls over automatically when GFrameCounter changes.
	 *
	 * Shared across all consumer sites — the cap is a global per-frame
	 * limit on debug-viz entity draws, not per-site.
	 */
	SEINARTSMOVEMENT_API bool TryReserveBudget();

	/**
	 * Combined per-call gate: camera lookup + cull + budget reserve. Use at
	 * per-entity / per-tick draw sites where you need the gate inline.
	 *
	 * Note: amortizes the camera lookup cost per call. For batch sites that
	 * already iterate a candidate list, prefer the explicit
	 * `GetActiveCameraView` / `PassesCameraCull` / `TryReserveBudget` triplet
	 * so the camera lookup happens once.
	 */
	SEINARTSMOVEMENT_API bool ShouldDrawAndReserve(UWorld* World, const FVector& WorldLocation);

	/**
	 * Compute the start point for a steering-vector arrow / line that should
	 * originate from the chassis FOOTPRINT EDGE instead of its geometric
	 * center. For large units (cars, tanks) center-origin arrows get visually
	 * occluded by the chassis mesh — short arrows disappear inside the model.
	 * Offsetting along the arrow's direction by FootprintRadius keeps the
	 * arrow visible past the mesh.
	 *
	 *   - Carrot lines: pass `Direction = Carrot - AgentPos`, then draw to
	 *     the carrot position. Line origin shifts outward; carrot endpoint
	 *     stays put.
	 *   - Force arrows (velocity, bias): pass `Direction = ForceVector`,
	 *     then draw to `OffsetOrigin + ForceVector` so the arrow's LENGTH
	 *     is preserved (representing magnitude).
	 *
	 * `Direction` does not need to be normalized — the helper handles that.
	 * If `Direction` is near-zero, falls back to `AgentPos` (with ZLift
	 * applied). `ZLift` is added to the Z component of the returned point
	 * for above-terrain rendering (typically 50 cm to clear ground geometry).
	 */
	SEINARTSMOVEMENT_API FVector ComputeFootprintOriginAlong(
		const FVector& AgentPos,
		const FVector& Direction,
		float FootprintRadius,
		float ZLift);
}

#endif // UE_ENABLE_DEBUG_DRAWING
