/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBlobFormation.h
 * @brief   Blob formation — a compact deterministic cluster around one anchor.
 *
 *          The framework default (preserves the historic single-destination move:
 *          the mass-select single-anchor model). The base layout starts every
 *          member on the anchor; the shared resolver's deterministic separation
 *          pass turns that degenerate input into the compact no-overlap slot set
 *          used by both preview and commit. This is what the removed
 *          `bFormationSpreadEnabled = false` used to produce. Inherits the base
 *          blob layout unchanged — no overrides needed.
 */

#pragma once

#include "CoreMinimal.h"
#include "Formations/SeinFormation.h"
#include "SeinBlobFormation.generated.h"

/**
 * Sends every selected unit toward one destination anchor as a compact, no-overlap huddle. This
 * is the framework's out-of-the-box formation: a plain "everyone move here" order with no
 * authored shape.
 *
 * A "blob" is the degenerate formation shape — instead of spreading members across a grid, wedge,
 * box, or ring, it assigns all of them the single order anchor as their target, and faces the
 * group by rotating its forward axis from the current centroid toward that anchor. The units do
 * not literally receive the same final slot: the shared resolver deterministically separates the
 * coincident base positions before dispatch, so preview and first path request use the same packed
 * cluster around the anchor. The hard collision floor remains the runtime safety net. This is the
 * classic mass-select single-anchor move, and
 * matches what the removed bFormationSpreadEnabled = false toggle
 * used to produce. It inherits the base Sein Formation layout with no overrides — the base default
 * IS the blob. For shaped alternatives see the grid, box, wedge, ring, square, and slot formations.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Blob Formation"))
class SEINARTSCOREENTITY_API USeinBlobFormation : public USeinFormation
{
	GENERATED_BODY()

	// Intentionally empty: the base USeinFormation default layout IS the blob
	// (every member → Target.Anchor). Exists as a concrete, designer-pickable class.
};
