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
 *
 *          Snapshot continuation is intentionally narrower than native
 *          construction: only the exact USeinMoveToProxy graph emitted by
 *          the standard Blueprint async node is checkpointable. A directly
 *          constructed/observerless action still runs, but capture refuses
 *          it instead of guessing how custom callbacks should be resumed.
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
struct FSeinMoveToActionCodec;

#if WITH_DEV_AUTOMATION_TESTS
namespace UE::SeinARTSTests
{
	struct FMoveToActionContinuationTestAccess;
}
#endif

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
	/** The unit is mechanically stuck: its applied step stayed ~zero against a
	 *  footprint-blocked direction, the hold-escape ladder forced a repath and
	 *  attempted nav escape legs, and three attempts exhausted (no escape
	 *  target / escape leg itself held / arrival outside the escape ring) — or
	 *  the per-order escape budget ran out on a recurring pin. Distinct from
	 *  `PathNotFound` so AI scripts can react differently (abandon order vs
	 *  retry destination): Stranded means "we tried to physically break free
	 *  and couldn't," PathNotFound means "we never found a plannable path." */
	Stranded            UMETA(DisplayName = "Stranded"),
	/** The async node was invoked outside bootstrap materialization or a
	 *  deterministic simulation callback, so no latent action was registered. */
	InvalidExecutionContext UMETA(DisplayName = "Invalid Execution Context")
};

UCLASS()
class SEINARTSMOVEMENT_API USeinMoveToAction : public USeinLatentAction
{
	GENERATED_BODY()

	friend class USeinMovementSubsystem;
	friend struct FSeinMoveToActionCodec;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct UE::SeinARTSTests::FMoveToActionContinuationTestAccess;
#endif

public:

	/** Set up a move toward `InDestination`. Acceptance radius is read from
	 *  `FSeinNavigationComponent::AcceptanceRadius` on first TickAction. */
	void Initialize(const FFixedVector& InDestination);

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;
	virtual void OnFail(uint8 ReasonCode) override;
	virtual void OnTimelineAbandoned() override;

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
	FFixedPoint AcceptanceRadius = FFixedPoint::Zero;

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
	 *  distance (projection before the segment start clamps to that endpoint).
	 *  Storing the origin lets the drift predicate include an
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
	 *  would push forever. `BestDistToFinal` is the closest planar distance reached this approach
	 *  (a monotonic high-water, so jitter/orbit never resets the clock); `TimeStalledNearGoal` accrues
	 *  while near + not closing. Once it stalls a short while inside the tight band, the move arrives —
	 *  this is as near as the body fits. See TickAction. */
	FFixedPoint BestDistToFinal = FFixedPoint::FromInt(1000);
	FFixedPoint TimeStalledNearGoal = FFixedPoint::Zero;

	/** Movement-trace bookkeeping only (LogSeinMoveTrace): consecutive ticks the
	 *  INITIAL path request came back Throttled — the window a freshly-ordered
	 *  unit stands as a commanded statue (bHasTarget set, no path, no movement
	 *  tick, no idle tick). Never read by sim logic. */
	int32 InitialThrottleStreak = 0;

	/** HOLD-ESCAPE LADDER — the far-from-goal counterpart of the near-goal stall
	 *  failsafe above. A unit whose APPLIED step (post nav-floor persisted
	 *  Velocity) stays ~zero while its order commands motion mid-route has no
	 *  other exit in the codebase: the stall failsafe is final-leg-only and
	 *  repaths keep succeeding, so a wall face-pin holds forever (the straggler).
	 *  The ladder: accrue HoldTime; at each 0.3s boundary probe the commanded
	 *  direction's footprint passability (a PASSABLE probe = pivot/yield — never
	 *  escalate); first blocked boundary forces a repath (stage 1); the next
	 *  queries the nav for an escape target and walks there as a short internal
	 *  leg (stage 2); three exhausted attempts fail the move with Stranded.
	 *  All action-local state (TimeStalledNearGoal precedent) is canonical
	 *  continuation state while the order is active and dies when that order
	 *  ends.
	 *  DETECTION SCOPE: Tier-1 harness modes only, by construction — Tier-2
	 *  vehicle Ticks persist COMMANDED velocity (Forward × CurrentSpeed), so a
	 *  wall-pinned vehicle never reads held here (Wheeled/Tracked carry their
	 *  own reverse-unstick machinery). */
	FFixedPoint HoldTime = FFixedPoint::Zero;
	/** Next HoldTime boundary at which the ladder probes/escalates (0.3s steps). */
	FFixedPoint NextEscalationAt = FFixedPoint::Zero;
	/** Stage 1 (forced repath) already fired for the current hold episode. */
	bool bStage1Fired = false;
	/** One-shot: fold a forced repath into the next repath-block evaluation,
	 *  bypassing the Interval timer / OffPathOnly drift+min-interval gates.
	 *  Cleared after ANY attempt including Throttled (never sticky — stage 2
	 *  backstops a budget-swallowed stage 1). */
	bool bForceRepathNow = false;
	/** Escape leg in flight: `Path` temporarily holds [AgentPos → EscapeTarget]. */
	bool bEscapeMode = false;
	FFixedVector EscapeTarget = FFixedVector::ZeroVector;
	/** Escape-leg acceptance radius (entry-gated so the leg can never instant-arrive). */
	FFixedPoint EscapeAcceptanceRadius = FFixedPoint::Zero;
	/** Per-attempt hold clock while the escape leg itself is walked. */
	FFixedPoint EscapeHoldTime = FFixedPoint::Zero;
	/** CONSECUTIVE failed escape attempts within the current stuck episode
	 *  (no-answer / leg held / arrival outside the ring). Episode-scoped: reset
	 *  by escape success AND by genuine resumed motion. At 3 the move fails
	 *  with ESeinMoveFailureReason::Stranded. */
	int32 EscapeAttempts = 0;
	/** TOTAL escape legs installed over this order's lifetime — never reset.
	 *  The terminating backstop for the walk→pin→escape→resume oscillation: a
	 *  reproducible pin whose escapes SUCCEED (so EscapeAttempts never
	 *  accumulates) but whose resume re-plans into the same pin would cycle
	 *  forever; at 5 entries the next escalation fails Stranded instead. */
	int32 TotalEscapeEntries = 0;
	/** Resolved once at first-tick setup (Extents → FallbackFootprintRadius
	 *  cascade): the body radius the ladder probes with and the escape query
	 *  carries. */
	FFixedPoint FootprintRadius = FFixedPoint::Zero;
	/** Near-goal settle band² shared by the stall failsafe AND the ladder's
	 *  exclusion: max(3 × acceptance, footprint + 100cm). Body-aware on
	 *  purpose — a unit ordered flush against a wall stops ~footprint short of
	 *  its final waypoint no matter how small the authored acceptance is; that
	 *  stop belongs to the failsafe (settle: as near as the body fits), never
	 *  to the ladder (escaping it would oscillate forever). */
	FFixedPoint StallBand = FFixedPoint::Zero;

	/** BORROWED reference to the entity's PERSISTENT movement instance,
	 *  acquired on first tick from USeinMovementSubsystem's registry (CP2.1,
	 *  D-R2 — one instance per UNIT, not per order; the registry owns lifetime
	 *  and GC-rooting, this UPROPERTY just keeps it reachable while the action
	 *  runs). Owns the actual advance-along-path logic; OnMoveBegin is the
	 *  per-order reset point for its persistent kinematic state. */
	UPROPERTY()
	TObjectPtr<USeinMovement> Movement;

	/** Claimed before dispatching terminal movement cleanup because OnMoveEnd
	 *  may execute Blueprint and synchronously re-enter ability cancellation. */
	bool bMovementFinalized = false;

	void NotifyCompleted();
	void NotifyWaypointReached(int32 Index, int32 Total);
	void NotifyPartialPath();
	void NotifyPathRecomputed();

	/** Dispatch OnMoveEnd and clear order-local movement flags exactly once. */
	void FinalizeMovementOnce();
};
