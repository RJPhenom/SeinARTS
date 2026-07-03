/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceDefault.cpp
 * @brief   CLEAN-SLATE SKELETON. The shipped default local-avoidance plug, reset to a no-op so a new
 *          model can be built from the ground up. It is still REGISTERED like any avoidance impl
 *          (PreTick, selected via USeinARTSCoreSettings::AvoidanceClass), but ComputeAvoidance writes
 *          nothing — every unit's AvoidanceOutput stays at its no-op default (SteerDir = 0,
 *          SpeedScale = 1), so selecting this class is behaviourally IDENTICAL to AvoidanceClass=None.
 *          The former lateral-steer boids model was removed here (recoverable from git history).
 */

#include "Movement/SeinAvoidanceDefault.h"

void USeinAvoidanceDefault::ComputeAvoidance(USeinWorldSubsystem& /*World*/)
{
	// CLEAN SLATE — intentionally a no-op. This writes no steer, so every unit's AvoidanceOutput
	// stays at its default (SteerDir = 0, SpeedScale = 1) and this plug behaves exactly like
	// AvoidanceClass=None. Build the new avoidance model here. Contract for the rebuild:
	//   • Neighbours: read from World.GetCollisionSpatialHash() — a start-of-tick SNAPSHOT (this runs
	//     PreTick, before movement), so neighbour transforms/velocities are frozen for the pass.
	//   • Output: per moving unit, write ONLY that unit's own FSeinMovementComponent::AvoidanceOutput
	//     — SteerDir (a planar lateral bend, consumed by USeinMovement::ApplyAvoidanceSteer) and/or
	//     SpeedScale in [0,1] (brake-to-yield, consumed by GetAvoidanceSpeedScale). Leave a unit's
	//     output at its default to make that unit a no-op.
	//   • Determinism (non-negotiable): read only the immutable snapshot; write ONLY each unit's own
	//     output (no cross-neighbour writes); all fixed-point (FFixedPoint/FFixedVector, no float, no
	//     FMath). That is the SeinParallelFor body contract, so the pass can fan across worker threads
	//     and stay bit-identical to the serial path under `Sein.Sim.Parallel 0`.
	//   • The hard collision floor still owns no-overlap — avoidance only changes how crowds FLOW,
	//     never whether bodies can pass through each other (never soften it to steer).
}
