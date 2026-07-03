/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInfantryMovement.h
 * @brief   Infantry movement — momentum-aware seek with smooth turn-to-velocity.
 *
 *          Inherits from USeinBasicMovement for class-hierarchy continuity but is a Tier-1
 *          ComputeMotion policy (the base USeinMovement::Tick harness drives it). Reasons:
 *          - Infantry needs velocity momentum (read/written through
 *            FSeinMovementComponent::Velocity) so reorders preserve direction
 *            and magnitude.
 *            Set Acceleration / Deceleration high (e.g. 50–100× TopSpeed) on
 *            FSeinInfantryMovementData (the MovementClassData sub-data) and the
 *            response is near-instant — but you don't get a hard zero-snap on
 *            order changes.
 *          - Basic stays dog-simple as the no-momentum baseline (good for
 *            cursors, debug agents). Infantry is the smoothed variant.
 *
 *          Rotation: faces the travel direction; the base Tick harness ramps the
 *          turn toward it at MoveData.TurnRate and skips it when planar velocity is
 *          sub-epsilon so blocked / arrival ticks don't slew facing back to identity.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinBasicMovement.h"
#include "SeinInfantryMovement.generated.h"

UCLASS(meta = (DisplayName = "Infantry"))
class SEINARTSMOVEMENTPLUS_API USeinInfantryMovement : public USeinBasicMovement
{
	GENERATED_BODY()

public:
	virtual void OnMoveBegin(const FSeinMovementContext& Ctx) override;
	virtual FSeinMotion ComputeMotion_Implementation(USeinMoverHandle* Mover) override;

	/** Returns FSeinInfantryMovementData so the editor auto-attaches it to
	 *  MovementClassData when Infantry is selected (carries Acceleration /
	 *  Deceleration — see SeinInfantryMovementData.h). */
	virtual UScriptStruct* GetMovementDataStruct() const override;
	virtual FFixedPoint GetDeceleration(const FSeinMovementComponent* MovementData) const override;
};
