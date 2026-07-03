/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBlobFormation.h
 * @brief   Blob formation — every member shares the one destination anchor.
 *
 *          The framework default (preserves the historic single-destination move:
 *          the mass-select single-destination model where the hard collision floor packs
 *          units into a no-overlap cluster on arrival). This is what the removed
 *          `bFormationSpreadEnabled = false` used to produce. Inherits the base
 *          blob layout unchanged — no overrides needed.
 */

#pragma once

#include "CoreMinimal.h"
#include "Formations/SeinFormation.h"
#include "SeinBlobFormation.generated.h"

/**
 * Sends every selected unit to the exact same destination point, letting them pile up into a
 * tight, no-overlap huddle once they arrive. This is the framework's out-of-the-box formation:
 * a plain "everyone move here" order with no shape to it.
 *
 * A "blob" is the degenerate formation shape — instead of spreading members across a grid, wedge,
 * box, or ring, it assigns all of them the single order anchor as their target, and faces the
 * group by rotating its forward axis from the current centroid toward that anchor. The units do
 * not literally stack on the same coordinate: the deterministic hard-collision floor pushes
 * overlapping bodies apart on arrival (minimum-translation separation), so the crowd settles into
 * a packed cluster around the anchor. This is the classic mass-select single-destination move, and
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
