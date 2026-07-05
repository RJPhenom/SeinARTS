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

	/** Near-goal stall failsafe. A unit pinned within a tight band of a final waypoint it cannot
	 *  physically occupy (a nav-reachable cell whose body footprint is wall/crowd-blocked) never
	 *  satisfies the harness arrival — it is never within AcceptanceRadius, and it heads INTO the
	 *  obstacle so the overshoot guard (which needs "heading away") won't fire — so without this it
	 *  would push forever. `BestDistToFinalSq` is the closest planar distance² reached this approach
	 *  (a monotonic high-water, so jitter/orbit never resets the clock); `TimeStalledNearGoal` accrues
	 *  while near + not closing. Once it stalls a short while inside the tight band, the move arrives —
	 *  this is as near as the body fits. See TickAction. */
	FFixedPoint BestDistToFinalSq = FFixedPoint::FromInt(1000000);
	FFixedPoint TimeStalledNearGoal = FFixedPoint::Zero;

	/** Movement-trace bookkeeping only (LogSeinMoveTrace): consecutive ticks the
	 *  INITIAL path request came back Throttled — the window a freshly-ordered
	 *  unit stands as a commanded statue (bHasTarget set, no path, no movement
	 *  tick, no idle tick). Never read by sim logic. */
	int32 InitialThrottleStreak = 0;

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
	void NotifyPathRecomputed();

	/** Reset transient sim state on the owner's FSeinMovementComponent when
	 *  the action terminates abnormally (cancel / fail). Currently clears
	 *  `bArrivalImminent` so AnimBPs don't show stale "approaching" state
	 *  after a mid-arrival cancellation. */
	void ResetTransientMoveState();
};
