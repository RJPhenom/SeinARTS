/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceDefault.h
 * @brief   The framework's shipped local-avoidance implementation — the
 *          SpringRTS/BAR-distilled lateral-steer boids model.
 *
 *          Per moving unit, reads neighbours from the collision spatial hash (a
 *          start-of-tick snapshot, since this runs BEFORE movement) and accumulates
 *          a purely-LATERAL steering nudge that bends the unit around other MOVABLE
 *          units before they collide. UNIT-TO-UNIT ONLY — static geometry (walls,
 *          buildings) contributes nothing; nav blocking already keeps units clear of
 *          those, so avoidance is a pure cohesion tool, never a pathing tool. The
 *          nudge is written to FSeinMovementComponent::AvoidanceOutput.SteerDir; the
 *          shipped model leaves AvoidanceOutput.SpeedScale at 1 (it bends heading, it
 *          does not yet brake-to-yield). The movement Tick consumes the steer via
 *          USeinMovement::ApplyAvoidanceSteer. The hard floor still guarantees
 *          no overlap — this just makes crowds FLOW instead of grind.
 *
 *          Model: turn-sign nudge, head-on weighting, distance falloff, closing-
 *          velocity gate, group cohesion (broker / cohesion-id), bulldoze-idle, a
 *          deterministic handle tie-break for the dead-ahead symmetry case, and
 *          Phase-D group-vs-group passing. All fixed-point, lockstep-safe.
 *
 *          The default for `USeinARTSCoreSettings::AvoidanceClass`. Subclass
 *          USeinAvoidance directly to ship a different model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinAvoidance.h"
#include "SeinAvoidanceDefault.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "Sein Avoidance Default"))
class SEINARTSMOVEMENT_API USeinAvoidanceDefault : public USeinAvoidance
{
	GENERATED_BODY()

public:

	/** One PreTick local-avoidance pass — see USeinAvoidance::ComputeAvoidance and
	 *  the file-header model notes. Reads the immutable start-of-tick snapshot, fans
	 *  the per-unit computation across worker threads (SeinParallelFor), writes each
	 *  unit's own AvoidanceOutput.SteerDir. Bit-identical serial under
	 *  `Sein.Sim.Parallel 0`. */
	virtual void ComputeAvoidance(USeinWorldSubsystem& World) override;
};
