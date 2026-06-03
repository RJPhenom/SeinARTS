/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicUnitMovement.h
 * @brief   Generic RTS-style movement -- seek + arrive along path with
 *          rotation toward velocity direction.
 *
 *          Sits between USeinBasicMovement (raw transform-along-path, no
 *          steering sugar) and the vehicle classes (momentum, kinematic
 *          arrival, curvature preview). This class is the AoE2 / StarCraft
 *          feel: units face the direction they're walking at TurnRate.
 *          Instant speed (no accel/decel).
 *
 *          Key differences from Basic:
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

public:
	virtual bool Tick(const FSeinMovementContext& Ctx) override;
};
