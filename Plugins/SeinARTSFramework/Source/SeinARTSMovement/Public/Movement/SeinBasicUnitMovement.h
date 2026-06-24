/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicUnitMovement.h
 * @brief   The RTS-default ground mover (StarCraft / AoE2 feel): seek + arrive
 *          along the path with kinematic arrival braking, facing the direction
 *          of travel at TurnRate.
 *
 *          As of the BP-authoring refactor this is a friendly-named MARKER class —
 *          the RTS loop it used to implement now lives in the base
 *          USeinMovement::BP_Tick_Implementation (the BP-authoring default), so
 *          selecting it is equivalent to the framework default. Kept as a clear
 *          picker entry and the natural parent for BP modes that want the RTS feel
 *          as a starting point. The concrete vehicle modes (Wheeled / Tracked /
 *          Hover / Flight) live in the SeinARTSMovementPlus extension.
 *
 *          The RTS feel, for reference (now provided by the base loop):
 *            - Kinematic arrival: brakes to a stop at the acceptance ring
 *              (v² = 2·a·d) instead of a flat top speed.
 *            - Faces the actual movement delta each tick, clamped by TurnRate.
 *            - Persists Velocity from the moved delta (non-strafing).
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
