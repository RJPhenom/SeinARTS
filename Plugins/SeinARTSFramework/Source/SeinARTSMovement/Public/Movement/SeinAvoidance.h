/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidance.h
 * @brief   Abstract base class for pluggable local-avoidance implementations.
 *
 *          USeinAvoidance owns one tick's full SOFT local steering pass for one
 *          world: per moving unit it reads a start-of-tick neighbour snapshot and
 *          writes that unit's avoidance OUTPUT (FSeinMovementPayload::AvoidanceOutput
 *          — a lateral steer + a speed-yield scale). It is the ONLY thing the PreTick
 *          avoidance system (FSeinAvoidanceSystem) talks to: that system is a thin
 *          delegator that calls ComputeAvoidance() once per tick, at PreTick priority
 *          Avoidance (after the collision broadphase rebuild, before movement runs).
 *
 *          Avoidance is the SOFT layer ABOVE the hard penetration floor
 *          (USeinCollisionResolver): it bends crowds so they FLOW instead of grinding,
 *          but it never guarantees no-overlap — the floor does that, separately. The
 *          movement Tick CONSUMES the output (USeinMovement::ApplyAvoidanceSteer for the
 *          steer, GetAvoidanceSpeedScale for the yield); avoidance never moves anything
 *          itself. Data flows: avoidance writes the field → movement reads it.
 *
 *          Configured via plugin settings (`USeinARTSCoreSettings::AvoidanceClass`).
 *          The framework ships `USeinAvoidanceDefault` (a lateral-steer boids
 *          model). Game teams can subclass or replace it entirely
 *          (a different boids model, a flow-field follower's separation pass, etc.)
 *          without touching any other framework code. Mirrors the pluggable Navigation
 *          / Collision-resolver / Fog-of-War pattern (abstract base + shipped default,
 *          class chosen in settings). NOT ORCA — see the project movement notes for why
 *          velocity-obstacle avoidance is out of scope for RTS unit counts.
 *
 *          Determinism: an avoidance impl is a deterministic UObject — fixed-point only
 *          (no float / FMath / rand), GC-rooted by the movement subsystem's UPROPERTY.
 *          The contract ComputeAvoidance MUST honour (the whole reason this runs at
 *          PreTick, not inline in movement): read ONLY the immutable start-of-tick
 *          snapshot (broadphase + neighbour transforms/velocities, frozen at PreTick)
 *          and write ONLY each unit's own AvoidanceOutput. That is exactly the
 *          SeinParallelFor body contract (immutable reads + disjoint per-self writes),
 *          so an impl is free to fan the per-unit work across worker threads.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SeinAvoidance.generated.h"

class USeinWorldSubsystem;
class UWorld;

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Avoidance"))
class SEINARTSMOVEMENT_API USeinAvoidance : public UObject
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Lifecycle — called by USeinMovementSubsystem
	// ----------------------------------------------------------------------

	/** Called once when the movement subsystem instantiates this avoidance impl.
	 *  Default: no-op. Mirrors USeinCollisionResolver::OnInitialized. */
	virtual void OnInitialized(UWorld* /*World*/) {}

	/** Called after the delegating system stops and before this implementation
	 *  is detached, while its native module is still callable. Override to
	 *  release delegate bindings or native resources. Must be idempotent and
	 *  must not issue gameplay mutations. */
	virtual void OnDeinitialized() {}

	// ----------------------------------------------------------------------
	// Compute — the per-tick surface
	// ----------------------------------------------------------------------

	/** Run one simulation tick's FULL local-avoidance pass: for each moving unit,
	 *  read the start-of-tick neighbour snapshot and write that unit's
	 *  FSeinMovementPayload::AvoidanceOutput (lateral steer + speed-yield scale).
	 *  Called once per PreTick by FSeinAvoidanceSystem. Default: no-op (leaves every
	 *  unit's output at its zero-steer / unity-scale default → a world with no
	 *  avoidance). Subclasses override. */
	virtual void ComputeAvoidance(USeinWorldSubsystem& /*World*/) {}

	/** True only for an exact native implementation whose per-tick compute does
	 *  not mutate reflected fields on the policy UObject itself. Unknown native
	 *  and Blueprint implementations remain conservatively dirty-tracked. */
	virtual bool HasImmutableRuntimePolicyState() const { return false; }
};
