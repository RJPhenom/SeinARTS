/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinInfantryMovementData.h
 * @brief:   Per-class movement data for `USeinInfantryMovement`. Surfaces
 *           in the entity bridge via
 *           `FSeinMovementComponent::MovementClassData` when the designer
 *           picks USeinInfantryMovement as the movement class.
 *
 *           Marked with `SeinSubData` so it appears in the polymorphic
 *           sub-data picker on FSeinMovementComponent::MovementClassData
 *           but is filtered out of the entity bridge's top-level
 *           ComponentData picker — sub-data is not authored directly,
 *           it surfaces only as a child of the movement component.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "SeinInfantryMovementData.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, SeinSubData))
struct SEINARTSMOVEMENTPLUS_API FSeinInfantryMovementData : public FSeinComponent
{
	GENERATED_BODY()

	// Intentionally empty. USeinInfantryMovement drives entirely off the base
	// FSeinMovementComponent knobs (TopSpeed / Acceleration / Deceleration /
	// TurnRate); the old per-class TopRotationSpeed / LateralAcceleration /
	// LateralDeceleration fields were never read by any movement code and were
	// cut. Kept as a placeholder so infantry still gets a sub-data slot in the
	// MovementClassData picker — add genuinely infantry-specific tuning here
	// if/when the steering work defines a real need.
};

FORCEINLINE uint32 GetTypeHash(const FSeinInfantryMovementData& /*C*/)
{
	// No per-instance state — all instances are equivalent. Stable non-zero
	// constant so the struct stays usable as a hashed value / map key.
	return 0x5E10A11Cu;
}
