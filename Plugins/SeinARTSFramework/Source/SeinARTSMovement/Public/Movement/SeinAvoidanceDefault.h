/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAvoidanceDefault.h
 * @brief   The framework's shipped local-avoidance plug: a lateral-steer + brake-to-yield model.
 *
 *          One PreTick pass over all movers. Per moving unit it accumulates a sideways nudge away
 *          from qualifying neighbours (SteerDir) and eases cruise speed off as that nudge saturates
 *          (SpeedScale < 1, yield-by-braking) — both written to the unit's own AvoidanceOutput and
 *          consumed by the movement harness/policies. The gate chain that decides "qualifying":
 *          units only (never walls — nav owns statics), never formation-mates (two-layer group skip:
 *          same broker OR same per-order cohesion group); IDLE neighbours (defined as carrying no
 *          move order) fall to the collision floor by default, or when Idle Resolve is on the mover
 *          THREADS around them toward a goal-aligned gap (and an idle unit may itself step aside,
 *          writing an honest velocity so the anim layer and re-seek react); weight-priority (lighter
 *          yields to heavier), ahead-of-heading + genuinely-closing courses only, and nothing past
 *          the unit's own goal. Inside a few footprints of the goal the output releases outright
 *          (binary arrival release, not a fade) so path attraction and the collision floor own the
 *          endgame. Formation cohesion (the SpeedScale hold-back / catch-up pacing against the
 *          group's mean remaining distance) is keyed to honest motion, never to commanded
 *          velocity: the group mean averages members with real per-tick DISPLACEMENT, and the
 *          catch-up boost waits for real PROGRESS toward the goal — so neither a body-blocked
 *          nor a sideways-churned member is ever throttled harder into a jam.
 *          Deterministic throughout: fixed-point, snapshot reads, handle-index tiebreaks,
 *          parallel-safe (SeinParallelFor contract).
 *
 *          Tuning: model-shape constants in Project Settings → SeinARTS (Navigation|Avoidance,
 *          part of the settings determinism fingerprint); per-unit dials (AvoidanceStrength /
 *          AvoidanceWeight / bAvoidSameWeights) on FSeinMovementComponent. Swap the whole model by
 *          subclassing USeinAvoidance and picking your class in settings — this class is the
 *          shipped OPINION, not the seam.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinAvoidance.h"
#include "SeinAvoidanceDefault.generated.h"

class USeinWorldSubsystem;

/**
 * The out-of-the-box local-avoidance model: moving units bend around crossing/oncoming traffic
 * (lateral steer), brake as the weave gets dense (speed yield), pack with their own formation
 * group instead of dodging it, thread around idle stragglers via a goal-aligned gap-seek when Idle
 * Resolve is on (else the collision floor shoves them aside), and release entirely on final approach
 * so arrivals settle instead of
 * orbiting. Ship different behavior by subclassing Sein Avoidance and selecting it in settings.
 */
UCLASS(meta = (DisplayName = "Sein Avoidance Default"))
class SEINARTSMOVEMENT_API USeinAvoidanceDefault : public USeinAvoidance
{
	GENERATED_BODY()

public:

	/** One PreTick local-avoidance pass over all movers — writes each unit's own
	 *  AvoidanceOutput (SteerDir + SpeedScale). See the file docstring for the model
	 *  and USeinAvoidance::ComputeAvoidance for the seam contract. */
	virtual void ComputeAvoidance(USeinWorldSubsystem& World) override;
};
