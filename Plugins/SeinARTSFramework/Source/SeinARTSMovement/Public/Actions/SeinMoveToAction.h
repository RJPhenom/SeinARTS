/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.h
 * @brief   Latent action that moves a sim entity along a USeinNavigation-
 *          produced path. Implementation-agnostic: the action never touches
 *          grids, pathfinders, or A* internals — it only consumes FSeinPath.
 *
 *          Kinematics are read from FSeinMovementComponent (TopSpeed /
 *          Acceleration / TurnRate); pathfinding + acceptance + repath knobs
 *          are read from FSeinNavigationComponent. Steering is minimal:
 *          seek toward next waypoint with an arrive radius at the final
 *          waypoint.
 */

#pragma once

#include "CoreMinimal.h"
#include "Abilities/SeinLatentAction.h"
#include "SeinPathTypes.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinMoveToAction.generated.h"

class USeinMoveToProxy;
class USeinMovement;

/** Reasons a move can fail. Passed via USeinLatentAction::Fail() reason code. */
UENUM(BlueprintType)
enum class ESeinMoveFailureReason : uint8
{
	None                UMETA(DisplayName = "None"),
	PathNotFound        UMETA(DisplayName = "Path Not Found"),
	EntityDestroyed     UMETA(DisplayName = "Entity Destroyed"),
	NoMovementComponent UMETA(DisplayName = "No Movement Component"),
	NoNavigation        UMETA(DisplayName = "No Navigation"),
	Cancelled           UMETA(DisplayName = "Cancelled"),
	/** Chassis was stranded — entered the escape-nudge fallback (driving up
	 *  the WallDistance gradient toward open space) but couldn't make
	 *  meaningful progress before the escape timer expired, or no passable
	 *  neighbor existed to nudge toward. Distinct from `PathNotFound` so
	 *  AI scripts can react differently (e.g., abandon order vs retry
	 *  destination): Stranded means "we tried the escape route and it
	 *  didn't help," PathNotFound means "we never found a plannable path." */
	Stranded            UMETA(DisplayName = "Stranded")
};

UCLASS()
class SEINARTSMOVEMENT_API USeinMoveToAction : public USeinLatentAction
{
	GENERATED_BODY()

public:

	/** Set up a move toward `InDestination`. Acceptance radius is read from
	 *  `FSeinNavigationComponent::AcceptanceRadius` on first TickAction. */
	void Initialize(const FFixedVector& InDestination);

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;
	virtual void OnFail(uint8 ReasonCode) override;

	/** Optional observer — receives Completed/Failed/Waypoint/Cancelled events. */
	TWeakObjectPtr<USeinMoveToProxy> Observer;

	UPROPERTY()
	FSeinPath Path;

	bool IsPathValid() const { return Path.bIsValid; }

	/** Index of the waypoint the entity is currently heading toward. Public so
	 *  debug rendering can draw "entity → current waypoint → remaining path". */
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

private:

	FFixedVector Destination;

	/** Resolved at first TickAction from FSeinNavigationComponent::AcceptanceRadius. */
	FFixedPoint AcceptanceRadiusSq = FFixedPoint::Zero;

	int32 CurrentWaypointIndex = 0;
	bool bPathResolved = false;

	/** True when this move's Destination is an AUTHORITATIVE position (a cover slot)
	 *  that overrules the coarse nav bake. Queried ONCE at first-tick setup from
	 *  USeinWorldSubsystem::AuthoritativeDestinationResolver and carried on the
	 *  movement tick context so ResolveNavCollision lets the unit stand on it even
	 *  when its cell is bake-blocked. See root CLAUDE.md invariant #6. */
	bool bAuthoritativeDestination = false;

	/** Agent's position at the moment the current `Path` was committed
	 *  (initial FindPath or a successful repath). Used by OffPathOnly
	 *  drift detection as the implicit start of the polyline.
	 *
	 *  Why this exists: `USeinNavigationAStar::BuildSmoothedPath`
	 *  deliberately skips `CellPath[0]` to avoid a visible "hook" at move
	 *  start. So `Path.Waypoints[0]` is NOT the agent's starting position
	 *  — it's the first LoS-collapsed cell DOWN the path, potentially
	 *  many meters ahead. An agent walking from its start position toward
	 *  `Waypoints[0]` is on-path, but a naive perpendicular-to-polyline
	 *  measurement reads it as "off-path" by the full agent→Waypoints[0]
	 *  distance (T<0 in `OffPathSegDistSqXY` clamps to the segment-start
	 *  endpoint). Storing the origin lets the drift calc include an
	 *  implicit `[PathOriginAgentPos → Waypoints[0]]` segment, capturing
	 *  the "approach the first waypoint along its line" semantic. */
	FFixedVector PathOriginAgentPos = FFixedVector::ZeroVector;

	/** Time since the last repath fired (Interval mode). Reset to zero
	 *  whenever a fresh path is committed. Compared against
	 *  `FSeinNavigationComponent::RepathInterval`. */
	FFixedPoint TimeSinceLastRepath = FFixedPoint::Zero;

	/** Consecutive interval-repath failures since the last successful repath
	 *  (or move-start). When this hits
	 *  `FSeinNavigationComponent::RepathFailureLimit` the action fails with
	 *  `PathNotFound` instead of marching toward an increasingly stale
	 *  path. Reset on every successful repath. */
	int32 ConsecutiveRepathFailures = 0;

	/** Escape-nudge fallback state. When the chassis ends up in a position
	 *  A* can't expand from (`Path.bIsPartial && Waypoints.Num() == 1`, or
	 *  repeated repath failures), we override `Path` with a single waypoint
	 *  pointing at the highest-WD passable neighbor cell and let the normal
	 *  carrot/steering pipeline drive the chassis toward it. Once the
	 *  chassis reaches a cell with WD ≥ Required (back in C-space), or the
	 *  escape timer expires without meaningful progress, we exit. Exit
	 *  modes: success → force immediate repath; failure → `Stranded`.
	 *
	 *  `bInEscapeMode` true while the override is active. `EscapeTimer`
	 *  counts seconds since escape entry. `EscapeStartPos` is the chassis
	 *  position when we entered escape — compared against current pos to
	 *  detect "isn't moving even with the nudge target set." */
	bool bInEscapeMode = false;
	FFixedPoint EscapeTimer = FFixedPoint::Zero;
	FFixedVector EscapeStartPos = FFixedVector::ZeroVector;

	/** Near-goal stall settle (failsafe). A unit can get pinned a footprint-width
	 *  short of a final waypoint it can never physically occupy — a nav-reachable
	 *  cell whose body footprint is blocked by an adjacent wall (the nav/collision
	 *  seam). The movement's own arrival can't fire there: it's never within
	 *  AcceptanceRadius, and the overshoot graceful-stop needs `heading AWAY` while
	 *  a pinned unit heads straight INTO the obstacle. So it seeks that point
	 *  forever — and for face-velocity movements (infantry weld facing to seek
	 *  direction) "seek forever" renders as "spin in place forever."
	 *
	 *  This is the missing exit: once the agent is within `StallVicinityRadiusSq`
	 *  of the final waypoint AND makes no further radial progress for
	 *  `StallSettleDuration` (see TickAction), the move arrives — this spot is as
	 *  close as the body can get. Class-agnostic (lives on the action, not a
	 *  movement subclass, so it protects every mode) and complements the
	 *  escape-nudge fallback above: escape handles stuck-at-START (can't plan from
	 *  the start cell); this handles stuck-at-GOAL (plans + approaches fine, but
	 *  can't physically finish).
	 *
	 *  `StallVicinityRadiusSq` is resolved once at first-tick setup from acceptance
	 *  + footprint. `BestDistToFinalSq` is the closest planar distance² to the
	 *  final waypoint reached within the current near-goal approach — a monotonic
	 *  high-water mark, so jitter/orbit around the closest reachable point never
	 *  resets the stall clock; only genuine fresh closing does. It re-arms (resets
	 *  to the live distance) whenever the agent is OUTSIDE the vicinity, so a
	 *  detour / repath / escape that moves it away measures a clean fresh approach.
	 *  The "fresh closing" test runs in ACTUAL distance against `StallProgressBand`
	 *  (~half a footprint, set at setup): the agent must close more than that band
	 *  past its best to re-arm. (A squared additive epsilon was distance-dependent
	 *  and vanished to sub-mm at band ranges, so jitter/creep re-armed forever and
	 *  units shoved the goal endlessly.) `TimeStalledNearGoal` accumulates while
	 *  near + not improving. */
	FFixedPoint StallVicinityRadiusSq = FFixedPoint::Zero;
	FFixedPoint BestDistToFinalSq = FFixedPoint::FromInt(1000000);
	FFixedPoint StallProgressBand = FFixedPoint::Zero;
	FFixedPoint TimeStalledNearGoal = FFixedPoint::Zero;

	/** PILE-UP ARRIVAL. A unit near its destination that has stopped making
	 *  progress (its stall clock has reached a short delay) AND is pressed against
	 *  a neighbour which has come to REST between it and the goal has effectively
	 *  arrived - it is only shoving the back of a settled crowd. The progress gate
	 *  is what keeps a still-flowing unit from collapsing: a unit advancing toward
	 *  the destination keeps closing, so its stall clock stays ~0 and it never
	 *  qualifies - it flows in and fills the pack. Propagates the settle outward
	 *  from the first unit to stop, so a whole pack rests in a quick ripple, and it
	 *  is robust to loose / imperfect packing (it asks "are the units AHEAD
	 *  stopped?"). Shares the crowd-sized `StallVicinityRadiusSq` band with the
	 *  stall settle. `StallFootprintRadius` is the unit's collision radius, cached
	 *  at setup to size the neighbour query. */
	FFixedPoint StallFootprintRadius = FFixedPoint::Zero;

	/** BORROWED reference to the entity's PERSISTENT movement instance,
	 *  acquired on first tick from USeinMovementSubsystem's registry (CP2.1,
	 *  D-R2 — one instance per UNIT, not per order; the registry owns lifetime
	 *  and GC-rooting, this UPROPERTY just keeps it reachable while the action
	 *  runs). Owns the actual advance-along-path logic; OnMoveBegin is the
	 *  per-order reset point for its persistent kinematic state. */
	UPROPERTY()
	TObjectPtr<USeinMovement> Movement;

	void NotifyCompleted();
	void NotifyWaypointReached(int32 Index, int32 Total);
	void NotifyPartialPath();

	/** Reset transient sim state on the owner's FSeinMovementComponent when
	 *  the action terminates abnormally (cancel / fail). Currently clears
	 *  `bArrivalImminent` so AnimBPs don't show stale "approaching" state
	 *  after a mid-arrival cancellation. */
	void ResetTransientMoveState();
};
