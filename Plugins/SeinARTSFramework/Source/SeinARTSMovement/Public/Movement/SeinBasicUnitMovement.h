/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicUnitMovement.h
 * @brief   Ultra-basic ground unit: translate toward the path at top speed and face the direction
 *          of travel at TurnRate. The framework's default ground feel.
 *
 *          A ComputeMotion policy over the shared base Tick harness — in fact it IS the base default
 *          policy (translate + face-velocity), kept as a clear, BP-parentable picker entry. The two
 *          tunables it reads are Speed (TopSpeed) and TurnRate; NO acceleration ramp, arrival brake,
 *          reverse, or slope tilt — those "nice feel" concerns are the Infantry mode's job in the
 *          SeinARTS Movement+ extension, not the framework defaults.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinMovement.h"
#include "SeinBasicUnitMovement.generated.h"

UCLASS(meta = (DisplayName = "Basic Unit (Seek + Arrive + Face)"))
class SEINARTSMOVEMENT_API USeinBasicUnitMovement : public USeinMovement
{
	GENERATED_BODY()

public:
	virtual FSeinMotion ComputeMotion_Implementation(USeinMoverHandle* Mover) override;
	virtual bool SupportsExactIdleMutationTracking() const override
	{
		return GetClass() == StaticClass();
	}
};
