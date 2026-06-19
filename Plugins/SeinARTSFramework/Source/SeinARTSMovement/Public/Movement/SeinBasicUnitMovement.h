/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicUnitMovement.h
 * @brief   The RTS-default ground mover: seek + arrive along the path with
 *          kinematic arrival braking and rotation toward velocity direction.
 *
 *          The StarCraft / AoE2 feel — units face the direction they're walking
 *          at TurnRate. Builds on USeinBasicMovement (raw transform-along-path)
 *          by adding face-velocity turning and a kinematic speed ramp. The
 *          concrete vehicle modes (Wheeled / Tracked / Hover / Flight) live in
 *          the SeinARTSMovementPlus extension, not here.
 *
 *          Key differences from Basic:
 *            - Kinematic arrival: brakes to a stop at the acceptance ring
 *              (v² = 2·a·d) instead of moving at a flat top speed.
 *            - Rotates the entity to face actual movement delta each tick,
 *              clamped by `MoveData.TurnRate`.
 *            - Persists `MoveData.Velocity` from the actual moved delta
 *              (non-strafing convention).
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "SeinBasicUnitMovement.generated.h"

UCLASS(meta = (DisplayName = "Basic Unit (RTS Default)"))
class SEINARTSMOVEMENT_API USeinBasicUnitMovement : public USeinMovement
{
	GENERATED_BODY()

	// The RTS default loop now lives in USeinMovement::BP_Tick_Implementation (the
	// BP-authoring default — see Movement_Mode_Authoring_Plan.md). This named mode
	// adds no behavior of its own; selecting it is equivalent to the framework default.
	// Kept as a distinct, friendly-named picker entry and the natural parent for BP
	// modes that want the RTS feel as their starting point.
};
