/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinInfantryMovementData.h
 * @brief:   Per-class movement data for `USeinInfantryMovement`. Surfaces
 *           in the entity bridge via
 *           `FSeinMovementPayload::MovementClassData` when the designer
 *           picks USeinInfantryMovement as the movement class.
 *
 *           Marked with `SeinSubData` so it appears in the polymorphic
 *           sub-data picker on FSeinMovementPayload::MovementClassData
 *           but is filtered out of the entity bridge's top-level
 *           ComponentData picker — sub-data is not authored directly,
 *           it surfaces only as a child of the movement component.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinPayload.h"
#include "Types/FixedPoint.h"
#include "SeinInfantryMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSMOVEMENTPLUS_API FSeinInfantryMovementData : public FSeinPayload
{
	GENERATED_BODY()

	/** Acceleration rate (world units per second²) — how quickly current speed ramps UP toward the
	 *  target. Feeds the smoothstep speed model (StepSpeedToward); high values give snappy infantry.
	 *  Moved off the now-bare FSeinMovementPayload (2026-07-02) — accel/decel are per-mode tuning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Acceleration = FFixedPoint::FromInt(750);

	/** Deceleration rate (world units per second²) — how quickly current speed ramps DOWN, and the
	 *  kinematic arrival-brake rate into the final waypoint. Typically >= Acceleration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement",
		meta = (ClampMin = "0.0"))
	FFixedPoint Deceleration = FFixedPoint::FromInt(750);
};

FORCEINLINE uint32 GetTypeHash(const FSeinInfantryMovementData& C)
{
	uint32 H = GetTypeHash(C.Acceleration);
	H = HashCombine(H, GetTypeHash(C.Deceleration));
	return H;
}
