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
 *          the unit's own goal. Inside a few footprints of the goal the output fades linearly
 *          (outer → inner arrival radii) so path attraction and the collision floor own the
 *          endgame. Formation cohesion (the SpeedScale hold-back / catch-up pacing against the
 *          group's mean remaining distance) is keyed to honest motion, never to commanded
 *          velocity: the group mean averages members with real per-tick DISPLACEMENT, and the
 *          catch-up boost waits for real PROGRESS toward the goal — so neither a body-blocked
 *          nor a sideways-churned member is ever throttled harder into a jam.
 *          Deterministic throughout: fixed-point, snapshot reads, handle-index tiebreaks,
 *          parallel-safe (SeinParallelFor contract).
 *
 *          Tuning: the model-shape constants live on this class's CDO (edit via a Blueprint subclass
 *          slotted in Project Settings → AvoidanceClass). The three model-AGNOSTIC harness knobs
 *          (Moving Speed Floor / Bend Cap / Idle Dodge Step Speed) stay in plugin settings — the
 *          movement harness consumes them, not this model. Per-unit dials (AvoidanceStrength /
 *          AvoidanceWeight / bAvoidSameWeights) on FSeinMovementPayload. Swap the whole model by
 *          subclassing USeinAvoidance and picking your class in settings — this class is the shipped
 *          OPINION, not the seam. Determinism: these CDO/asset-authored values are captured by
 *          AvoidanceClass's fingerprinted path plus identical compiled/asset content (not per-machine
 *          config), so they leave the settings determinism fingerprint.
 */

#pragma once

#include "CoreMinimal.h"
#include "Movement/SeinAvoidance.h"
#include "Types/FixedPoint.h"
#include "SeinAvoidanceDefault.generated.h"

class USeinWorldSubsystem;

/**
 * The out-of-the-box local-avoidance model: moving units bend around crossing/oncoming traffic
 * (lateral steer), brake as the weave gets dense (speed yield), pack with their own formation
 * group instead of dodging it, thread around idle stragglers via a goal-aligned gap-seek when Idle
 * Resolve is on (else the collision floor shoves them aside), and fade out on final approach so
 * arrivals settle instead of orbiting. Ship different behavior by subclassing Sein Avoidance and selecting it in settings.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Sein Avoidance Default"))
class SEINARTSMOVEMENT_API USeinAvoidanceDefault : public USeinAvoidance
{
	GENERATED_BODY()

public:

	/** One PreTick local-avoidance pass over all movers — writes each unit's own
	 *  AvoidanceOutput (SteerDir + SpeedScale). See the file docstring for the model
	 *  and USeinAvoidance::ComputeAvoidance for the seam contract. */
	virtual void ComputeAvoidance(USeinWorldSubsystem& World) override;
	virtual bool HasImmutableRuntimePolicyState() const override
	{
		return GetClass() == StaticClass();
	}

	// ====================================================================================
	// Model tuning — authored on this class's CDO. To tune, subclass this as a Blueprint, set the
	// values in the class-defaults panel, and slot that class in Project Settings > AvoidanceClass.
	// A different model carries its OWN knobs on its own class (no shared-knob assumption). The three
	// model-AGNOSTIC knobs (Moving Speed Floor / Bend Cap / Idle Dodge Step Speed) stay in plugin
	// settings — the movement harness consumes them, not this model. Determinism: code/asset-authored
	// and captured by AvoidanceClass's fingerprinted path + identical content, not per-machine config.
	// ====================================================================================

	/** How far ahead in time a moving unit looks for neighbours to steer around. It perceives others
	 *  out to twice its footprint plus its speed times this many seconds, so faster units watch farther
	 *  ahead. Default 0.5 seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Lookahead Seconds", ClampMin = "0.0"))
	FFixedPoint AvoidanceLookaheadSeconds = FFixedPoint::One / FFixedPoint::FromInt(2);

	/** How far a neighbour's influence reaches, measured in multiples of the two units' combined
	 *  footprint. Steering fades to zero by this multiple. Default 5. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Falloff Radii", ClampMin = "0.0"))
	FFixedPoint AvoidanceFalloffRadii = FFixedPoint::FromInt(5);

	/** How much of last tick's steering direction is carried into this tick, from 0 to 1, to keep
	 *  motion smooth instead of jittery. Higher is smoother but slower to react. Default 0.7. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Smooth Keep", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceSmoothKeep = FFixedPoint::FromInt(7) / FFixedPoint::FromInt(10);

	/** The minimum weight given to a neighbour that is not coming head-on. A unit moving with the flow
	 *  or crossing perpendicular still counts at least this much, so it is never ignored entirely.
	 *  Default 0.1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Head-On Base", ClampMin = "0.0"))
	FFixedPoint AvoidanceHeadOnBase = FFixedPoint::One / FFixedPoint::FromInt(10);

	/** How close to its goal a unit begins fading avoidance-steering, measured in footprints. At
	 *  this distance avoidance starts ramping down; by the inner radius (Arrival Fade Inner Radii)
	 *  it is fully off and the collision resolver and path attraction own the final approach. The
	 *  fade prevents units from abruptly dropping all avoidance on arrival and slamming into
	 *  neighbours already at the destination. Default 3. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Arrival Release Radii", ClampMin = "0.0"))
	FFixedPoint AvoidanceArrivalReleaseRadii = FFixedPoint::FromInt(3);

	/** How close to its goal a unit fully stops avoidance-steering, measured in footprints. Inside
	 *  this radius avoidance is completely off. Between this and Arrival Release Radii the output
	 *  fades linearly. Must be less than Arrival Release Radii to produce a fade; if equal or
	 *  greater, the release is a hard cut (legacy behaviour). Default 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Arrival Fade Inner Radii", ClampMin = "0.0"))
	FFixedPoint AvoidanceArrivalFadeInnerRadii = FFixedPoint::One;

	/** The cap on how strong the accumulated sideways nudge can get before per-unit strength scaling
	 *  and smoothing are applied. Keeps a crowded unit from being shoved sideways too hard in one tick.
	 *  Default 2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Max Steer Magnitude", ClampMin = "0.0"))
	FFixedPoint AvoidanceMaxSteerMagnitude = FFixedPoint::FromInt(2);

	/** How firmly a MOVING unit steers around a stationary unit that's in its way.
	 *
	 *  0 = the mover plows straight through parked units and lets the collision layer shove them aside.
	 *  Above 0, a mover instead weaves around an idle unit whose Avoidance Weight qualifies
	 *  (heavier-or-equal) - so moving infantry route around an idle tank, while a moving tank still
	 *  plows through idle infantry (the lighter idler is ignored). Higher = firmer weave. The mover
	 *  can't circle the parked unit - the Bend Cap guarantees it keeps making forward progress.
	 *  Default 1 (idle-resolve on). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Idle Resolve Strength", ClampMin = "0.0"))
	FFixedPoint AvoidanceIdleResolveStrength = FFixedPoint::One;

	/** How strongly an IDLE unit steps aside for an approaching mover that's about to run it over.
	 *
	 *  0 = idle units never move on their own (a mover plows through them, the collision layer shoves
	 *  them). Above 0, an idle unit shuffles sideways to make a lane for an approaching mover whose
	 *  Avoidance Weight qualifies (heavier-or-equal) - so idle infantry step aside for a passing tank,
	 *  while an idle tank holds its ground for passing infantry. Only active while Idle Re-Seek is on:
	 *  the re-form owns walking the unit back to its slot once the mover has passed. Default 1
	 *  (idle-dodge on). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Idle Dodge Strength", ClampMin = "0.0"))
	FFixedPoint AvoidanceIdleDodgeStrength = FFixedPoint::One;

	/** How hard a unit brakes when it is swerving hard through traffic. 0 = never brake.
	 *
	 *  Yield-by-slowing, layered on top of yield-by-turning: steering saturation is the congestion
	 *  signal, and at full saturation the unit's cruise speed is multiplied by (1 - this), scaling
	 *  linearly in between. Applies only while a unit is actively avoiding; a clear unit always runs
	 *  full cruise. Default 0.25 - a unit swerving at its steering cap drops to 75 percent cruise. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Brake Strength", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceBrakeStrength = FFixedPoint::One / FFixedPoint::FromInt(4);

	/** How much a unit that has pulled ahead of its formation slows down so the group stays
	 *  together. 0 = leaders never wait.
	 *
	 *  At full deviation ahead of the group's mean progress, the front-runner's cruise speed is
	 *  multiplied by (1 - this). The group is the unit's command broker (its formation); lone units
	 *  are unaffected. Pairs with Cohesion Catch-Up Boost and Cohesion Range. Default 0.5. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Cohesion Hold-Back", ClampMin = "0.0", ClampMax = "1.0"))
	FFixedPoint AvoidanceCohesionHoldBack = FFixedPoint::One / FFixedPoint::FromInt(2);

	/** How much a unit that has fallen behind its formation speeds up to close the gap.
	 *  1 = laggards never hurry.
	 *
	 *  The cruise multiplier a lagging member ramps toward at full deviation behind the group's mean
	 *  progress. Values above 1 push a unit past its authored top speed - how a movement mode
	 *  physically honors that is the mode's own policy. Pairs with Cohesion Hold-Back and Cohesion
	 *  Range. Default 2 - stragglers sprint at up to double cruise to rejoin their formation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Cohesion Catch-Up Boost", ClampMin = "1.0"))
	FFixedPoint AvoidanceCohesionCatchUpBoost = FFixedPoint::FromInt(2);

	/** How strung out a formation must get, measured in bodies, before catch-up and hold-back
	 *  reach full strength.
	 *
	 *  Multiples of a unit's footprint radius, so the response is the same on a short hop and a long
	 *  march. A member starts reacting once it deviates from the group's mean progress by about 15
	 *  percent of this range and responds fully at the whole range. Default 8. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Cohesion Range (Footprints)", ClampMin = "1.0"))
	FFixedPoint AvoidanceCohesionRangeRadii = FFixedPoint::FromInt(8);

	/** How strongly two units on a genuine crossing course slide past each other.
	 *
	 *  When two movers are heading opposite ways and their goals are on opposite sides, they pick
	 *  opposite sides and curve past instead of running into each other or circling. This scales that
	 *  sideways slide. 0 turns the crossing slide-past off entirely (units fall back to the basic
	 *  side-step, which is what causes the head-on lock and orbiting). Default 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Do-Si-Do Strength", ClampMin = "0.0"))
	FFixedPoint AvoidanceDoSiDoStrength = FFixedPoint::One;

	/** How far apart two units' goals must be before the engine treats them as genuinely crossing.
	 *
	 *  Measured as a multiple of how far apart the two units currently are. Higher means the slide-past
	 *  only kicks in for units that really are trading places, so a crowd converging on one spot still
	 *  packs tightly instead of shoving sideways. Lower makes units more eager to treat a near-miss as
	 *  a crossing. Combined with the opposite-directions test. Default 1. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "Crossing Goal Divergence", ClampMin = "0.0"))
	FFixedPoint AvoidanceCrossingGoalDivergence = FFixedPoint::One;
};
