/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.h
 * @brief   Latent action that moves a sim entity along a USeinNavigation-
 *          produced path. Implementation-agnostic: the action never touches
 *          grids, pathfinders, or A* internals — it only consumes FSeinPath.
 *
 *          Kinematics are read from FSeinMovementPayload (TopSpeed /
 *          Acceleration / TurnRate); pathfinding + acceptance + repath knobs
 *          are read from FSeinNavigationPayload. Steering is minimal:
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
class USeinNavigation;
class USeinNavigationSubsystem;
class USeinWorldSubsystem;
struct FSeinEntity;
struct FSeinMovementPayload;
struct FSeinMovementContext;
struct FSeinNavigationPayload;
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

/** Stuck-recovery phase of one active Move To order: the hold-escape ladder's
 *  explicit state machine. The clocks and counters that ride alongside it are
 *  documented on USeinMoveToAction's ladder field block.
 *
 *    Free            → Holding          held tick outside the near-goal settle band
 *    Holding         → Free             any unheld tick (clears HoldTime, the boundary
 *                                       counter, and EscapeAttempts)
 *    Holding         → Holding          0.3s boundary, commanded direction PASSABLE
 *                                       (a pivot / yield policy zero: never escalate)
 *    Holding         → HoldingRepathed  0.3s boundary, commanded direction blocked
 *                                       (stage 1: one forced repath)
 *    HoldingRepathed → HoldingRepathed  boundary + blocked, but the nav offers no escape
 *                                       target or it sits inside the entry gate
 *                                       (EscapeAttempts++)
 *    HoldingRepathed → Escaping         boundary + blocked + target beyond the gate
 *                                       (stage 2: the order path is discarded and the
 *                                       internal escape leg installed; TotalEscapeEntries++)
 *    Escaping        → Free             leg arrived inside its ring; HoldTime and
 *                                       EscapeAttempts reset; the order re-resolves
 *    Escaping        → HoldingRepathed  leg arrived outside its ring, or the leg itself
 *                                       held 0.6s (EscapeAttempts++). HoldTime and the
 *                                       spent stage 1 are PRESERVED, so the first held
 *                                       tick after the re-resolve escalates straight
 *                                       back to stage 2 — that cadence is what bounds
 *                                       the three attempts.
 *    any             → Stranded (Fail)  EscapeAttempts reaches 3, or a fifth escape leg
 *                                       would be installed in one order
 *
 *  Escaping is reachable only through HoldingRepathed. While Escaping, `Path` is
 *  EMPTY: the harness drives the two-point leg [EscapeOrigin → EscapeTarget]
 *  (USeinMoveToAction::GetDrivenPath) and the tick epilogue skips the whole
 *  order-progress tail. HoldingRepathed with `bPathResolved == false` is the
 *  post-escape re-resolve in flight (the order path is re-planned from scratch). */
UENUM()
enum class ESeinMoveStuckPhase : uint8
{
	Free,
	Holding,
	HoldingRepathed,
	Escaping
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
	 *  `FSeinNavigationPayload::AcceptanceRadius` on first TickAction. */
	void Initialize(const FFixedVector& InDestination);

	virtual bool TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override;
	virtual void OnCancel() override;
	virtual void OnFail(uint8 ReasonCode) override;
	virtual void OnTimelineAbandoned() override;

	/** Optional observer — receives Completed/Failed/Waypoint/Cancelled events. */
	TWeakObjectPtr<USeinMoveToProxy> Observer;

	/** The ORDER's committed route. Empty for the whole of an escape leg (the
	 *  ladder discards it at escape entry and the resume re-plans it); the
	 *  polyline actually being driven is GetDrivenPath. */
	UPROPERTY()
	FSeinPath Path;

	/** Whether the ORDER path is committed and valid. False during an escape leg
	 *  even though the unit is driving a valid polyline — query GetDrivenPath
	 *  for "is the unit driving something". */
	bool IsPathValid() const { return Path.bIsValid; }

	/** Index of the waypoint the entity is currently heading toward. Public so
	 *  debug rendering can draw "entity → current waypoint → remaining path". */
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	/** Stuck-recovery phase (see ESeinMoveStuckPhase). Public for debug rendering
	 *  and tests; no sim logic reads it from outside the action. */
	ESeinMoveStuckPhase GetStuckPhase() const { return StuckPhase; }

	/** The polyline the harness drives THIS tick. Normally `Path`. While an escape
	 *  leg is in flight `Path` is empty and the two-point leg is rebuilt into
	 *  `Scratch` from EscapeOrigin / EscapeTarget — identical content every tick,
	 *  so the leg is never canonical state and never needs restoring. */
	const FSeinPath& GetDrivenPath(FSeinPath& Scratch) const;

private:
	enum class EInitialPathTickResult : uint8
	{
		Ready,
		Waiting,
		Terminal
	};

	enum class ERepathTickResult : uint8
	{
		Skipped,
		Continued,
		Terminal
	};

	FFixedVector Destination;

	/** Resolved at first TickAction from FSeinNavigationPayload::AcceptanceRadius. */
	FFixedPoint AcceptanceRadius = FFixedPoint::Zero;

	int32 CurrentWaypointIndex = 0;
	bool bPathResolved = false;

	/** True when this move's destination is accepted by Core's composed
	 *  authoritative-destination providers. Queried once at first-tick setup and
	 *  carried on the movement context so a coarse-bake false negative cannot
	 *  relocate the exact destination. */
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
	 *  `FSeinNavigationPayload::RepathInterval`. */
	FFixedPoint TimeSinceLastRepath = FFixedPoint::Zero;

	/** Consecutive interval-repath failures since the last successful repath
	 *  (or move-start). When this hits
	 *  `FSeinNavigationPayload::RepathFailureLimit` the action fails with
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
	 *  ends. The phase itself is explicit (ESeinMoveStuckPhase carries the
	 *  transition table); the fields below are its clocks and counters.
	 *  DETECTION SCOPE: Tier-1 harness modes only, by construction — Tier-2
	 *  vehicle Ticks persist COMMANDED velocity (Forward × CurrentSpeed), so a
	 *  wall-pinned vehicle never reads held here (Wheeled/Tracked carry their
	 *  own reverse-unstick machinery). */
	ESeinMoveStuckPhase StuckPhase = ESeinMoveStuckPhase::Free;
	/** Held-tick clock. Accrues while the applied planar step sits at or below
	 *  the moving-speed floor. Only genuine applied motion or a SUCCESSFUL escape
	 *  clears it: repaths never do, and a FAILED escape preserves it. */
	FFixedPoint HoldTime = FFixedPoint::Zero;
	/** Number of 0.3s hold boundaries the ladder has already consumed this
	 *  episode; the next fires at 0.3s × (HoldBoundariesFired + 1). Reset
	 *  whenever HoldTime is, and on every escape exit. */
	int32 HoldBoundariesFired = 0;
	/** One-shot: fold a forced repath into the next repath-block evaluation,
	 *  bypassing the Interval timer / OffPathOnly drift+min-interval gates.
	 *  Cleared after ANY attempt including Throttled (never sticky — stage 2
	 *  backstops a budget-swallowed stage 1). Not ladder-only: authored-tuning
	 *  refreshes raise it too. */
	bool bForceRepathNow = false;
	/** Escape leg endpoints while Escaping: the agent position at install and
	 *  the nav-supplied target. The harness drives [EscapeOrigin → EscapeTarget]
	 *  through GetDrivenPath; `Path` itself is empty for the whole leg. */
	FFixedVector EscapeOrigin = FFixedVector::ZeroVector;
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
	/** Near-goal settle band shared by the stall failsafe AND the ladder's
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

	/** Acquire the persistent movement instance and commit the first path.
	 *  Waiting preserves a throttled action for retry; Terminal means the
	 *  planner outcome already failed the action. */
	EInitialPathTickResult ResolveInitialPath(
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World,
		FSeinEntity& Entity,
		FSeinMovementPayload& MovementData,
		const FSeinNavigationPayload* NavigationData,
		USeinNavigation* Navigation,
		USeinNavigationSubsystem* NavigationSubsystem);

	/** Evaluate and, when due, commit one interval/off-path repath before the
	 *  movement tick. Returns Terminal only when the failure limit ends the move. */
	ERepathTickResult TickRepath(
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World,
		FSeinEntity& Entity,
		FSeinMovementPayload& MovementData,
		const FSeinNavigationPayload* NavigationData,
		USeinNavigation* Navigation,
		USeinNavigationSubsystem* NavigationSubsystem);

	/** Advance the temporary escape leg and restore the order path state when
	 *  that leg succeeds or exhausts. Returns true only for terminal failure. */
	bool TickEscapeLeg(
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World,
		FSeinEntity& Entity,
		FSeinMovementPayload& MovementData,
		bool bReachedEnd);

	/** Advance the held-unit escalation ladder after ordinary movement.
	 *  Returns true only when the ladder has terminally stranded the move. */
	bool TickHoldEscapeLadder(
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World,
		FSeinEntity& Entity,
		FSeinMovementPayload& MovementData,
		const FSeinNavigationPayload* NavigationData,
		USeinNavigation* Navigation,
		bool bReachedEnd);

	/** Publish the final-waypoint brake-zone state consumed by presentation. */
	void UpdateArrivalImminent(
		const FSeinEntity& Entity,
		FSeinMovementPayload& MovementData,
		bool bReachedEnd) const;

	/** Settle a final-leg unit that has stopped closing inside its body-aware
	 *  arrival band. May promote bReachedEnd at the exact stall threshold. */
	void TickNearGoalStall(
		FFixedPoint DeltaTime,
		USeinWorldSubsystem& World,
		const FSeinEntity& Entity,
		const FSeinMovementContext& MovementContext,
		bool& bReachedEnd);

	/** Apply settled-destination authority and terminal callbacks for a reached
	 *  order. Returns false when the order is still active. */
	bool CompleteReachedOrder(
		USeinWorldSubsystem& World,
		bool bReachedEnd);

	/** Dispatch OnMoveEnd and clear order-local movement flags exactly once. */
	void FinalizeMovementOnce();

	/** Component-owned terminal-tick refresh for authored navigation/extents or
	 *  movement-class changes. Ordinary kinematic tuning remains live-read. */
	void RefreshAuthoredComponentTuning(
		USeinWorldSubsystem& World,
		bool bRefreshMovementClass,
		bool bForcePathRefresh);
};
