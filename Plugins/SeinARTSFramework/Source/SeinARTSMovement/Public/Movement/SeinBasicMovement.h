/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicMovement.h
 * @brief   Ultra-basic mover: translate toward the path at top speed, no rotation.
 *
 *          A ComputeMotion policy over the shared base Tick harness — it returns "head to the
 *          current waypoint at terrain-scaled top speed" and holds facing (bUpdateFacing = false).
 *          No rotation, no momentum, no accel/brake. The null/invalid fallback, so "works out of
 *          the box" is preserved for any unit that doesn't pick a specific movement class.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "SeinBasicMovement.generated.h"

UCLASS(meta = (DisplayName = "Basic (Seek + Arrive)"))
class SEINARTSMOVEMENT_API USeinBasicMovement : public USeinMovement
{
	GENERATED_BODY()

public:
	virtual FSeinMotion ComputeMotion_Implementation(USeinMoverHandle* Mover) override;
};
