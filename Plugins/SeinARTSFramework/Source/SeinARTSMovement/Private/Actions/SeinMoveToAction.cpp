/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.cpp
 */

#include "Actions/SeinMoveToAction.h"
#include "Abilities/SeinMoveToProxy.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Movement/SeinMovement.h"
#include "SeinMovementSubsystem.h"   // persistent movement-instance registry (CP2.1)

#include "Abilities/SeinAbility.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Settings/PluginSettings.h"     // terrain-type → speed multiplier lookup
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Engine/World.h"
#include "Simulation/SeinMovementTraceLog.h"  // [ARRIVE]/[THROTTLE] movement-trace events
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMove, Log, All);

namespace
{
	/** Fixed chord tolerance (world units) for flattening typed Arc path segments into the
	 *  drivable waypoint backbone (FSeinPath::FlattenToWaypoints). Intentionally a COMPILE-TIME
	 *  constant, never a per-run tunable: the flatten must produce an identical waypoint count on
	 *  every peer, so a value that could differ across machines would be a silent desync. ~5 cm
	 *  sagitta reads smooth at RTS camera distance, and it only affects a plain follower driving an
	 *  arc path — a curve-aware mode reads the exact segments instead. */
	const FFixedPoint GArcFlattenChordError = FFixedPoint::FromInt(5);

	/** Planar (XY) distance² from point `Q` to segment `S..E`. Z is ignored —
	 *  the sim is XY-driven and Z drift on slopes shouldn't trigger spurious
	 *  off-path repaths. Closest-point-on-segment is the projection of Q onto
	 *  the segment line, clamped to the [0, 1] segment range. Degenerate
	 *  zero-length segments degrade to a point-distance check. */
	FFixedPoint OffPathSegDistSqXY(const FFixedVector& Q, const FFixedVector& S, const FFixedVector& E)
	{
		const FFixedPoint EX = E.X - S.X;
		const FFixedPoint EY = E.Y - S.Y;
		const FFixedPoint LenSq = EX * EX + EY * EY;
		const FFixedPoint QX = Q.X - S.X;
		const FFixedPoint QY = Q.Y - S.Y;
		if (LenSq <= FFixedPoint::Epsilon)
		{
			return QX * QX + QY * QY; // segment collapsed to a point
		}
		FFixedPoint T = (QX * EX + QY * EY) / LenSq;
		if (T < FFixedPoint::Zero) T = FFixedPoint::Zero;
		else if (T > FFixedPoint::One) T = FFixedPoint::One;
		const FFixedPoint PX = S.X + EX * T;
		const FFixedPoint PY = S.Y + EY * T;
		const FFixedPoint DX = Q.X - PX;
		const FFixedPoint DY = Q.Y - PY;
		return DX * DX + DY * DY;
	}

	/** Minimum planar (XY) distance² from `Point` to the polyline through
	 *  `Waypoints`, treating `PathOrigin` as the implicit FIRST endpoint
	 *  of the polyline.
	 *
	 *  Why the implicit prefix: `USeinNavigationAStar::
	 *  BuildSmoothedPath` deliberately skips `CellPath[0]` (to avoid a
	 *  visible "hook" at move start). Result: `Waypoints[0]` is the first
	 *  LoS-collapsed cell DOWN the path, NOT the agent's starting
	 *  position. For paths through open ground the smoother can collapse
	 *  20+ cells into one segment, putting `Waypoints[0]` many meters
	 *  ahead.
	 *
	 *  Without the implicit prefix, an agent walking straight from its
	 *  starting position toward `Waypoints[0]` reads as "off-path" by
	 *  the full agent→Waypoints[0] distance: T<0 in `OffPathSegDistSqXY`
	 *  clamps to the segment-start endpoint, so the closest "point on
	 *  segment" is Waypoints[0] itself. Pre-fix, OffPathOnly mode
	 *  measured ~86 spurious repaths per 10s of motion on the test
	 *  scenario; the agent was ON its path the whole time but the
	 *  measurement said otherwise.
	 *
	 *  Iterates EVERY segment of the polyline (including the implicit
	 *  prefix), since "drift" measures against the entire planned route,
	 *  not just the remaining portion. Empty waypoints returns Zero
	 *  (interpreted as "no path drift" — repath gate falls through). */
	FFixedPoint OffPathMinDistSqToPolyline(
		const FFixedVector& Point,
		const FFixedVector& PathOrigin,
		const TArray<FFixedVector>& Waypoints)
	{
		const int32 N = Waypoints.Num();
		if (N == 0) return FFixedPoint::Zero;

		// Implicit prefix segment: [PathOrigin → Waypoints[0]]. Captures
		// the "approach the first waypoint along its line" semantic that
		// the smoother's CellPath[0] skip would otherwise lose.
		FFixedPoint MinDistSq = OffPathSegDistSqXY(Point, PathOrigin, Waypoints[0]);

		// Actual polyline segments. Iterates [Waypoints[i] → Waypoints[i+1]]
		// for i in [0, N-2].
		for (int32 i = 0; i + 1 < N; ++i)
		{
			const FFixedPoint Sq = OffPathSegDistSqXY(Point, Waypoints[i], Waypoints[i + 1]);
			if (Sq < MinDistSq) MinDistSq = Sq;
		}
		return MinDistSq;
	}
}

void USeinMoveToAction::Initialize(const FFixedVector& InDestination)
{
	Destination = InDestination;
	// Squaring deferred — resolved on first TickAction once NavComp is available.
	AcceptanceRadiusSq = FFixedPoint::Zero;
	CurrentWaypointIndex = 0;
	bPathResolved = false;
	bAuthoritativeDestination = false;
	TimeSinceLastRepath = FFixedPoint::Zero;
	ConsecutiveRepathFailures = 0;
	BestDistToFinalSq = FFixedPoint::FromInt(1000000);
	TimeStalledNearGoal = FFixedPoint::Zero;
	HoldTime = FFixedPoint::Zero;
	NextEscalationAt = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10); // first ladder boundary: 0.3s
	bStage1Fired = false;
	bForceRepathNow = false;
	bEscapeMode = false;
	EscapeTarget = FFixedVector::ZeroVector;
	EscapeAcceptSq = FFixedPoint::Zero;
	EscapeHoldTime = FFixedPoint::Zero;
	EscapeAttempts = 0;
	TotalEscapeEntries = 0;
	FootprintRadius = FFixedPoint::Zero;
	StallBandSq = FFixedPoint::Zero;
	Path.Clear();
	Movement = nullptr;
	bMovementFinalized = false;
}

bool USeinMoveToAction::TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Sein_MoveTo_TickAction);
	FSeinEntity* Entity = World.GetEntityMutable(OwnerEntity);
	if (!Entity)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::EntityDestroyed));
		return true;
	}

	// Post-decomposition: the legacy FSeinMovementData was split into
	// FSeinMovementComponent (kinematics + runtime velocity/arrival state) and
	// FSeinNavigationComponent (footprint + nav-layer + acceptance + repath).
	// MoveComp is required for the action to function at all; NavComp is
	// soft-required — its absence forces fallback defaults for acceptance,
	// repath cadence, and footprint radius. (Most entity classes will author both,
	// but a "no nav" entity authored only with a movement component should
	// still be drivable by abilities that pass an explicit destination.)
	FSeinMovementComponent* MoveComp =
		World.GetComponentMutable<FSeinMovementComponent>(
			OwnerEntity);
	if (!MoveComp)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}
	const FSeinNavigationComponent* NavComp = World.GetComponent<FSeinNavigationComponent>(OwnerEntity);

	// Mark "actively driven" each tick. Idempotent set rather than first-tick-
	// only because cheap and survives reorders to TickAction's early structure.
	// Cleared by ResetTransientMoveState on cancel/fail/arrival, so AnimBPs
	// (and anything reading FSeinMovementComponent::bHasTarget) see "input
	// released" the moment the action ends — even while Velocity coasts toward
	// zero.
	MoveComp->bHasTarget = true;
	// Publish the current order's resolved goal to the component so PreTick systems
	// (avoidance arrival-release, cohesion laggard detection) can see it — they run
	// without this action's context. Idempotent, same rationale as bHasTarget; NOT
	// cleared at end (a "last ordered goal" — readers gate on bHasTarget).
	MoveComp->TargetLocation = Destination;

	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(&World);
	USeinNavigationSubsystem* NavSub = World.GetWorld()
		? World.GetWorld()->GetSubsystem<USeinNavigationSubsystem>()
		: nullptr;

	// First-tick setup: acquire the movement, then either FindPath or
	// synthesize a straight-line path depending on the movement's
	// `BypassPathfinding()` answer. Movement comes first so we can ask it.
	if (!bPathResolved)
	{
		// Acquire the entity's PERSISTENT movement instance (CP2.1, D-R2) from
		// the movement subsystem's registry — one instance per UNIT, not per
		// order, so subclass kinematic state (steer, ramps) survives across
		// orders by construction and the driver can tick the same instance
		// idle between orders. OnMoveBegin below remains the per-order reset
		// point (every shipped subclass resets its per-order state there).
		// The registry owns lifetime/GC-rooting; this action only borrows.
		USeinMovementSubsystem* MoveSub = World.GetWorld()
			? World.GetWorld()->GetSubsystem<USeinMovementSubsystem>()
			: nullptr;
		Movement = MoveSub
			? MoveSub->GetOrCreateMovementInstance(OwnerEntity, *MoveComp)
			: nullptr;
		if (!Movement)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
			return true;
		}

		// Path resolution — delegate to the movement. The movement owns the
		// planning pipeline: default impl matches today's behavior (cell A*
		// for ground, straight-line for flying), but subclasses can compose
		// nav primitives differently (e.g. wheeled fits a kinematic curve
		// over the cell A* output, emitting arc-tagged segments suited to
		// its turn dynamics). Result codes map 1:1 to the prior NavSub
		// dispatch:
		//   Throttled    → wait, retry next tick (no failure)
		//   NoNavigation → fail with NoNavigation
		//   NotFound     → fail with PathNotFound
		//   Found        → check bIsPartial + commit
		FSeinPlanPathContext PlanCtx{
			*Entity,
			MoveComp,
			NavComp,
			Destination,
			Nav,
			NavSub,
			OwnerEntity,
			&World    // World subsystem for the Extents-cascade footprint lookup.
		};
		ESeinPathResult Result;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Sein_MoveTo_InitialPlanPath);
			Result = Movement->PlanPath(PlanCtx, Path);
		}

		// Initial-resolve diagnostic: log PlanPath outcome with start/end pose
		// + path shape summary so corridor no-op cases (chassis sits still,
		// "Moving → Completed/Cancelled" sequence) can be traced. Pairs with
		// LogSeinNavigationAStar's cellA* logs for the full
		// "what did the planner do for this move order" picture.
		{
			const TCHAR* ResultStr =
				(Result == ESeinPathResult::Found)        ? TEXT("Found")        :
				(Result == ESeinPathResult::Throttled)    ? TEXT("Throttled")    :
				(Result == ESeinPathResult::NoNavigation) ? TEXT("NoNavigation") :
				(Result == ESeinPathResult::NotFound)     ? TEXT("NotFound")     :
				TEXT("?");
			const FFixedVector StartPos = Entity->Transform.GetLocation();
			UE_LOG(LogSeinMove, Verbose,
				TEXT("MoveToAction initial PlanPath: result=%s start=(%.1f,%.1f) "
				     "end=(%.1f,%.1f) → waypoints=%d segments=%d bIsValid=%d "
				     "bIsPartial=%d (entity %s)"),
				ResultStr,
				StartPos.X.ToFloat(), StartPos.Y.ToFloat(),
				Destination.X.ToFloat(), Destination.Y.ToFloat(),
				Path.Waypoints.Num(), Path.Segments.Num(),
				Path.bIsValid ? 1 : 0, Path.bIsPartial ? 1 : 0,
				*OwnerEntity.ToString());
		}

		// Throttled means "no budget this tick; wait, retry next tick" —
		// return false WITHOUT setting bPathResolved. Dropping Movement just
		// clears this action's BORROWED ref (the persistent instance stays in
		// the registry; next tick re-acquires it).
		if (Result == ESeinPathResult::Throttled)
		{
#if !UE_BUILD_SHIPPING
			// Movement-trace event: how long this unit has stood as a commanded statue
			// waiting on the path budget (log at 5, then every 30 ticks — a streak past
			// ~5 means the budget is being starved by earlier-inserted requesters).
			++InitialThrottleStreak;
			if (InitialThrottleStreak == 5 || (InitialThrottleStreak % 30) == 0)
			{
				UE_LOG(LogSeinMoveTrace, Verbose, TEXT("[THROTTLE] t=%d h=%d:%d streak=%d"),
					World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation,
					InitialThrottleStreak);
			}
#endif
			Movement = nullptr;
			return false;
		}
#if !UE_BUILD_SHIPPING
		InitialThrottleStreak = 0;
#endif
		if (Result == ESeinPathResult::NoNavigation)
		{
			Fail(static_cast<uint8>(ESeinMoveFailureReason::NoNavigation));
			return true;
		}
		if (Result != ESeinPathResult::Found || Path.Waypoints.Num() == 0)
		{
			UE_LOG(LogSeinMove, Verbose,
				TEXT("MoveToAction initial PlanPath: failing with PathNotFound — "
				     "result=%d waypoints=%d (entity %s)"),
				static_cast<int32>(Result), Path.Waypoints.Num(),
				*OwnerEntity.ToString());
			Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
			return true;
		}

		// Diagnostic-only: spot the "bIsValid=true with 0 drivable segments"
		// case that produces the visible "Moving → Completed → Cancelled"
		// no-op sequence — wheeled path-replay sees 0 segments and instantly
		// classifies as "end of path." Logged here so the no-op is identified
		// in the log stream without changing behavior; the structural fix
		// (e.g., invalidate the path in FindPath) is a separate change once
		// we've confirmed the diagnosis from the logs.
		if (Movement->GetMinTurnRadius(MoveComp) > FFixedPoint::Zero && Path.Segments.Num() == 0)
		{
			UE_LOG(LogSeinMove, Verbose,
				TEXT("MoveToAction initial PlanPath: vehicle path has 0 drivable "
				     "segments but is being committed (entity %s, dest=(%.1f,%.1f), "
				     "waypoints=%d, bIsValid=%d, bIsPartial=%d) — expect the "
				     "chassis to no-op and the action to NotifyCompleted instantly. "
				     "Diagnostic only — no behavior change here."),
				*OwnerEntity.ToString(),
				Destination.X.ToFloat(), Destination.Y.ToFloat(),
				Path.Waypoints.Num(),
				Path.bIsValid ? 1 : 0,
				Path.bIsPartial ? 1 : 0);
		}

		// Surface partial-path commits to the proxy so BPs can react
		// (UI toast, alternate cursor, cancel-and-retry). The action
		// continues to the partial endpoint regardless — OnCompleted
		// will still fire on arrival.
		if (Path.bIsPartial)
		{
			UE_LOG(LogSeinMove, Verbose,
				TEXT("Initial path is PARTIAL — destination unreachable, routing to closest cell (entity %s)"),
				*OwnerEntity.ToString());
			NotifyPartialPath();
		}

		// Expand any typed (Arc / Jump / Field / AbstractEdge) segments into the drivable waypoint
		// backbone so the built-in follower can drive them with no follower-loop changes. Self-
		// guarded + inert for the shipped all-Straight case (leaves Waypoints untouched); a
		// curve-aware Tier-2 mode ignores the flattened backbone and reads the exact segments.
		Path.FlattenToWaypoints(GArcFlattenChordError);

		bPathResolved = true;
		CurrentWaypointIndex = 0;
		// Capture the agent's position at the moment this path was
		// committed. Bypass paths (flying) use AgentPos as their first
		// waypoint, so origin = waypoint[0] there; for A* paths the
		// smoother skipped CellPath[0], so origin gives the OffPathOnly
		// drift detector the implicit start of the polyline. Either way,
		// PathOrigin = current agent pos at commit time.
		PathOriginAgentPos = Entity->Transform.GetLocation();

		// Resolve acceptance radius from the unit's navigation component —
		// properly a per-unit characteristic (footprint / turn radius), not
		// a per-call concern. Designers tune it on the nav component.
		// Falls back to 50cm when NavComp absent so commands still complete
		// instead of pinning at "almost arrived" forever.
		const FFixedPoint Acceptance = NavComp
			? NavComp->AcceptanceRadius
			: FSeinNavigationComponent::DefaultArrivalAcceptance();
		AcceptanceRadiusSq = Acceptance * Acceptance;

		// Body radius (once per order) + the shared near-goal settle band: the
		// stall failsafe SETTLES inside it, the hold-escape ladder is EXCLUDED
		// from it — one body-aware expression so there is never a gap (a pin
		// neither owns → the old silent stand) nor an overlap (both fire).
		// max() with the footprint because a unit ordered flush against a wall
		// stops ~footprint short of its final waypoint regardless of how small
		// the authored acceptance is; +100cm absorbs C-space rounding.
		FootprintRadius = USeinMovement::ResolveCollisionRadius(&World, OwnerEntity, NavComp);
		FFixedPoint StallBand = Acceptance * FFixedPoint::FromInt(3);
		const FFixedPoint BodyBand = FootprintRadius + FFixedPoint::FromInt(100);
		if (BodyBand > StallBand) { StallBand = BodyBand; }
		StallBandSq = StallBand * StallBand;

		// Authoritative destination: is this move's target a position that overrules
		// the coarse nav bake (a cover slot)? Queried ONCE here (not per-tick) and
		// carried on the movement tick context so ResolveNavCollision lets the unit
		// stand on it. Unbound (cover absent) → false. See root CLAUDE.md #6.
		bAuthoritativeDestination = World.AuthoritativeDestinationResolver.IsBound()
			&& World.AuthoritativeDestinationResolver.Execute(Destination);

		FSeinMovementContext BeginCtx{
			*Entity,
			MoveComp,
			NavComp,
			Path,
			CurrentWaypointIndex,
			AcceptanceRadiusSq,
			DeltaTime,
			Nav,
			&World,
			OwnerEntity
		};
		// Resolve the entity's collision footprint cascade
		// (Extents → NavComp->FallbackFootprintRadius → 0) BEFORE OnMoveBegin so
		// the movement's footprint-aware ResolveNavCollision is fully wired
		// from the very first tick. Single-shot lookup; the cache is
		// entity-stable across the move action.
		Movement->CacheFootprintFromContext(BeginCtx);
		if (UWorld* UnrealWorld = World.GetWorld())
		{
			if (USeinMovementSubsystem* MovementSub =
				UnrealWorld->GetSubsystem<USeinMovementSubsystem>())
			{
				MovementSub->MarkMovementStateDirty(OwnerEntity);
			}
		}
		Movement->OnMoveBegin(BeginCtx);
	}

	if (!Movement)
	{
		// Defensive: should never happen post-bPathResolved, but fail cleanly
		// rather than crash.
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}

	// Repath check — runs BEFORE the movement tick so a fresh path takes
	// effect this same frame. See ESeinRepathMode.
	//
	// Flying movements (BypassPathfinding=true) skip repath entirely —
	// their straight-line path doesn't drift in any meaningful sense
	// (avoidance steers the unit off-line, but the *line itself* is still
	// the correct goal vector). Recomputing it every interval would just
	// truncate to "from where I am now to End," which is what the unit
	// would compute next tick anyway.
	//
	// Escape mode also skips repath entirely — while the hold-escape ladder
	// (below the movement tick) walks its short internal leg, `Path` holds
	// [AgentPos → EscapeTarget] and a successful interval repath would clobber
	// it mid-escape (repaths keep SUCCEEDING in the pinned state — that is the
	// pathology). Escape exit restores pathing via bPathResolved = false.
	if (bEscapeMode)
	{
		// fall through to the movement tick below — the ladder owns Path.
	}
	else if (Movement->BypassPathfinding())
	{
		TimeSinceLastRepath = FFixedPoint::Zero;
		// fall through to the movement tick below — no repath block to run.
	}
	else
	{
	TimeSinceLastRepath = TimeSinceLastRepath + DeltaTime;
	// Repath cadence + mode live on the navigation component; absent NavComp
	// disables repathing (mode treated as no-op, falls through to the
	// movement tick). The legacy struct co-mingled these with kinematics; the
	// split puts pathfinding concerns where they belong.
	if (!NavComp)
	{
		// No nav component — skip the repath block entirely. The initial
		// path remains in force; if the agent drifts, no recovery happens.
		// Same fall-through path as the bypass-pathfinding branch.
	}
	else
	{
	// NavComp is non-null in this branch, so the repath knobs need no null-guard —
	// only the zero-field default remains (a designer leaving a field at 0 gets the
	// built-in default: ¼s interval / 3 failures / 75cm off-path threshold).
	const ESeinRepathMode RepathMode = NavComp->RepathMode;
	const FFixedPoint RepathInterval = NavComp->RepathInterval > FFixedPoint::Zero
		? NavComp->RepathInterval : FFixedPoint::FromInt(1) / FFixedPoint::FromInt(4);
	const int32 RepathFailureLimit = NavComp->RepathFailureLimit > 0
		? NavComp->RepathFailureLimit : 3;
	const FFixedPoint OffPathThreshold = NavComp->OffPathThreshold > FFixedPoint::Zero
		? NavComp->OffPathThreshold : FFixedPoint::FromInt(75);
	switch (RepathMode)
	{
	case ESeinRepathMode::Interval:
	{
		// bForceRepathNow: the hold-escape ladder's stage 1 — bypasses the
		// interval timer for one attempt. Cleared after ANY attempt below
		// (including Throttled — never sticky through a starved budget; the
		// ladder's stage 2 backstops a swallowed stage 1).
		if ((TimeSinceLastRepath >= RepathInterval || bForceRepathNow) && Nav && NavSub)
		{
			bForceRepathNow = false;
			FSeinPlanPathContext PlanCtx{
				*Entity,
				MoveComp,
				NavComp,
				Destination,
				Nav,
				NavSub,
				OwnerEntity,
				&World
			};
			FSeinPath NewPath;
			const ESeinPathResult RepathResult = Movement->PlanPath(PlanCtx, NewPath);
			if (RepathResult == ESeinPathResult::Throttled)
			{
				// No budget this tick. Reset TimeSinceLastRepath so we wait
				// another full Interval before trying again, instead of
				// retrying every tick — a previous version of this code
				// claimed retry-every-tick was a feature ("don't lose the
				// repath opportunity"), but it caused catastrophic budget
				// starvation: once an active unit's repath got throttled,
				// every subsequent tick would re-attempt and consume budget
				// that should have gone to first-time path requests from
				// other units waiting to start moving. With this reset,
				// throttled repath is a graceful skip — the unit walks the
				// existing (slightly stale) path while new starts get
				// priority access to the budget.
				//
				// Don't bump ConsecutiveRepathFailures here — throttling
				// is not a path-finding failure.
				TimeSinceLastRepath = FFixedPoint::Zero;
				break;
			}
			if (RepathResult == ESeinPathResult::Found && NewPath.Waypoints.Num() > 0)
			{
				// Swap in the fresh path and reset the waypoint cursor —
				// NewPath.Waypoints[0] is offset from current position so
				// starting at 0 means "head to the next intended carrot."
				// Movement's Velocity (on FSeinMovementComponent) is preserved
				// so the unit doesn't lose momentum at the swap.
				Path = NewPath;
				// Expand typed segments into the drivable backbone (self-guarded no-op for the
				// shipped all-Straight case). See the initial-commit flatten for rationale.
				Path.FlattenToWaypoints(GArcFlattenChordError);
				CurrentWaypointIndex = 0;
				// Re-anchor the OffPathOnly drift origin to the agent's
				// current position. This is the Path.Start fed to FindPath
				// above, so the implicit [PathOrigin → Waypoints[0]] prefix
				// in the drift calc matches the path's actual starting line.
				PathOriginAgentPos = Entity->Transform.GetLocation();
				ConsecutiveRepathFailures = 0;
				UE_LOG(LogSeinMove, Verbose,
					TEXT("Repath (Interval): %d new waypoints from (%.1f,%.1f)%s"),
					NewPath.Waypoints.Num(),
					Entity->Transform.GetLocation().X.ToFloat(),
					Entity->Transform.GetLocation().Y.ToFloat(),
					Path.bIsPartial ? TEXT(" [PARTIAL]") : TEXT(""));
				NotifyPathRecomputed(); if (Path.bIsPartial) NotifyPartialPath();
			}
			else
			{
				// Repath failure. Bump the consecutive-failure counter and
				// keep the stale path for now — single-tick blockages
				// (a unit briefly crossing the corridor) are routine and
				// shouldn't fail the move. Once we cross RepathFailureLimit
				// in a row, the world has demonstrably changed and the
				// stale path is no longer trustworthy: fail with
				// PathNotFound rather than march toward a dead end.
				++ConsecutiveRepathFailures;
				UE_LOG(LogSeinMove, Verbose,
					TEXT("Repath (Interval) failed: attempt %d/%d (entity %s)"),
					ConsecutiveRepathFailures, RepathFailureLimit, *OwnerEntity.ToString());
				if (ConsecutiveRepathFailures >= RepathFailureLimit)
				{
					UE_LOG(LogSeinMove, Warning,
						TEXT("Repath (Interval): %d consecutive failures — failing move (entity %s, dest=(%.1f,%.1f))"),
						ConsecutiveRepathFailures,
						*OwnerEntity.ToString(),
						Destination.X.ToFloat(), Destination.Y.ToFloat());
					Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
					return true;
				}
			}
			TimeSinceLastRepath = FFixedPoint::Zero;
		}
		break;
	}

	case ESeinRepathMode::OffPathOnly:
	{
		// Off-path detection: minimum perpendicular XY distance from the
		// agent to the planned polyline. When that drift exceeds the
		// per-unit threshold (avoidance pushed us off course, a building
		// dropped onto our line, etc.), recompute. Cheap O(N) scan over
		// path segments — typical RTS paths have ≤ 50 waypoints, so the
		// per-tick cost is trivial. The expensive `Nav->FindPath` only
		// fires when threshold is actually exceeded.
		//
		// `TimeSinceLastRepath` doubles as a min-time-between-attempts gate
		// here. Without it, drift > threshold + budget-throttled = retry
		// every tick = budget starvation for first-time path requests
		// from other units. Floor of 100ms (~3 ticks at 30Hz) before the
		// next drift check actually fires the request.
		const FFixedPoint MinAttemptInterval = FFixedPoint::FromInt(1) / FFixedPoint::FromInt(10); // 100ms
		// bForceRepathNow (hold-escape stage 1) bypasses BOTH the min-attempt
		// gate AND the drift gate: a nav-floor-pinned unit sits ON its polyline
		// (drift ~0), which is exactly why OffPathOnly never rescues it.
		if (TimeSinceLastRepath < MinAttemptInterval && !bForceRepathNow) break;
		if (!Nav) break;

		const FFixedVector AgentPos = Entity->Transform.GetLocation();
		const FFixedPoint DriftSq = OffPathMinDistSqToPolyline(AgentPos, PathOriginAgentPos, Path.Waypoints);

		// OffPathThreshold already resolved to its 75cm zero-field default upstream
		// (where RepathMode is read); here it's the live drift tolerance.
		const FFixedPoint ThresholdSq = OffPathThreshold * OffPathThreshold;

		if (DriftSq <= ThresholdSq && !bForceRepathNow) break;
		if (!NavSub) break;

		bForceRepathNow = false;
		FSeinPlanPathContext PlanCtx{
			*Entity,
			MoveComp,
			NavComp,
			Destination,
			Nav,
			NavSub,
			OwnerEntity,
			&World
		};
		FSeinPath NewPath;
		const ESeinPathResult RepathResult = Movement->PlanPath(PlanCtx, NewPath);
		if (RepathResult == ESeinPathResult::Throttled)
		{
			// No budget this tick. Reset TimeSinceLastRepath so we wait
			// MinAttemptInterval before trying again — without this reset,
			// a drifted-and-throttled unit would retry every tick and
			// saturate the path budget. Same starvation fix as the
			// Interval branch above. Don't bump ConsecutiveRepathFailures:
			// throttling isn't a path-finding failure.
			TimeSinceLastRepath = FFixedPoint::Zero;
			break;
		}
		if (RepathResult == ESeinPathResult::Found && NewPath.Waypoints.Num() > 0)
		{
			Path = NewPath;
			// Expand typed segments into the drivable backbone (self-guarded no-op for the shipped
			// all-Straight case). See the initial-commit flatten for rationale.
			Path.FlattenToWaypoints(GArcFlattenChordError);
			CurrentWaypointIndex = 0;
			// Re-anchor the OffPathOnly drift origin to AgentPos (same as
			// Req.Start fed to RequestPath above) so the implicit
			// [PathOrigin → Waypoints[0]] prefix in the drift calc matches
			// the new path's actual starting line.
			PathOriginAgentPos = AgentPos;
			ConsecutiveRepathFailures = 0;
			UE_LOG(LogSeinMove, Verbose,
				TEXT("Repath (OffPathOnly): drift=%.1fcm > threshold=%.1fcm, %d new waypoints from (%.1f,%.1f)%s"),
				SeinMath::Sqrt(DriftSq).ToFloat(),
				OffPathThreshold.ToFloat(),
				NewPath.Waypoints.Num(),
				AgentPos.X.ToFloat(),
				AgentPos.Y.ToFloat(),
				Path.bIsPartial ? TEXT(" [PARTIAL]") : TEXT(""));
			NotifyPathRecomputed(); if (Path.bIsPartial) NotifyPartialPath();
		}
		else
		{
			// Same fail-after-N policy as Interval mode. OffPathOnly only
			// fires when drift > threshold, so each failure here means
			// "drifted off course AND can't find a fresh path" — exactly
			// the silent-stale-path failure mode. RepathFailureLimit is
			// shared so designers tune one knob.
			++ConsecutiveRepathFailures;
			const int32 Limit = RepathFailureLimit;
			UE_LOG(LogSeinMove, Verbose,
				TEXT("Repath (OffPathOnly) failed: drift=%.1fcm, attempt %d/%d (entity %s)"),
				SeinMath::Sqrt(DriftSq).ToFloat(),
				ConsecutiveRepathFailures, Limit, *OwnerEntity.ToString());
			if (ConsecutiveRepathFailures >= Limit)
			{
				UE_LOG(LogSeinMove, Warning,
					TEXT("Repath (OffPathOnly): %d consecutive failures — failing move (entity %s, dest=(%.1f,%.1f))"),
					ConsecutiveRepathFailures,
					*OwnerEntity.ToString(),
					Destination.X.ToFloat(), Destination.Y.ToFloat());
				Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
				return true;
			}
		}
		// Reset min-attempt-interval gate after a non-throttled attempt
		// (success or failure both consumed budget). Next drift check
		// won't fire until MinAttemptInterval has elapsed.
		TimeSinceLastRepath = FFixedPoint::Zero;
		break;
	}
	}
	} // end else (NavComp present — repath knobs scoped here)
	} // end else (non-bypass repath block)

	// Delegate the per-tick advance. Movement mutates Entity.Transform and
	// CurrentWaypointIndex; returns true when the final waypoint is reached.
	// Terrain SPEED multiplier at the unit's current cell — sampled once per tick and
	// applied uniformly via USeinMovement::EffectiveTopSpeed. Independent of routing cost.
	// Default 1 (no nav / Default terrain), so it's behaviour-preserving until authored.
	FFixedPoint TerrainSpeedMult = FFixedPoint::One;
	if (Nav)
	{
		const int32 TerrainType = Nav->GetTerrainTypeAt(Entity->Transform.GetLocation());
		if (TerrainType != 0)
		{
			if (const USeinARTSCoreSettings* CoreSettings = GetDefault<USeinARTSCoreSettings>())
			{
				TerrainSpeedMult = CoreSettings->GetTerrainSpeedMultiplier(TerrainType);
			}
		}
	}

	const int32 PrevWaypoint = CurrentWaypointIndex;
	FSeinMovementContext TickCtx{
		*Entity,
		MoveComp,
		NavComp,
		Path,
		CurrentWaypointIndex,
		AcceptanceRadiusSq,
		DeltaTime,
		Nav,
		&World,
		OwnerEntity
	};
	// Escape-mode context overrides (ctx-local; the action members are preserved
	// for resume): the escape leg is never an authoritative destination (the
	// cover-slot exemption would re-target the nav floor at the escape cell),
	// and its acceptance is the entry-gated escape ring, not the order's.
	TickCtx.bAuthoritativeDestination = bEscapeMode ? false : bAuthoritativeDestination;
	if (bEscapeMode) { TickCtx.AcceptanceRadiusSq = EscapeAcceptSq; }
	TickCtx.TerrainSpeedMultiplier = TerrainSpeedMult;
	// Non-const: the near-goal stall-settle below can promote this to true to
	// force arrival when the unit is pinned short of an unreachable goal.
	if (UWorld* UnrealWorld = World.GetWorld())
	{
		if (USeinMovementSubsystem* MovementSub =
			UnrealWorld->GetSubsystem<USeinMovementSubsystem>())
		{
			MovementSub->MarkMovementStateDirty(OwnerEntity);
		}
	}
	bool bReachedEnd;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Sein_MoveTo_MovementTick);
		bReachedEnd = Movement->Tick(TickCtx);
	}

	// ESCAPE-LEG TICK EPILOGUE — while the hold-escape ladder's internal leg is
	// in flight, the whole order-progress tail below (waypoint notify,
	// bArrivalImminent, near-goal stall failsafe, completion) must NOT run:
	// the failsafe's final-leg gate is trivially true on the short escape path
	// (0.75s held would falsely Complete the order at the wall), and a true
	// return from the harness here means "reached the ESCAPE target", never
	// "reached the order destination".
	if (bEscapeMode)
	{
		const FFixedVector EscPos = Entity->Transform.GetLocation();
		bool bExitEscape = false;
		bool bAttemptFailed = false;
		const TCHAR* EscOutcome = TEXT("");

		if (bReachedEnd)
		{
			// Validate the arrival: the overshoot guard can misfire on the
			// escape ENTRY state (persisted Velocity ~0 + facing the wall
			// passes IsOvershootArrival) — a true return OUTSIDE the escape
			// ring is that misfire, not an escape.
			FFixedVector ToEsc = EscapeTarget - EscPos;
			ToEsc.Z = FFixedPoint::Zero;
			const bool bGenuine = ToEsc.SizeSquared() <= EscapeAcceptSq;
			bExitEscape = true;
			bAttemptFailed = !bGenuine;
			EscOutcome = bGenuine ? TEXT("done") : TEXT("overshoot");
		}
		else
		{
			// Per-attempt exhaustion: the escape leg itself is held (the floor
			// refuses even the escape direction) — abandon after 0.6s.
			FFixedVector EscVel = MoveComp->Velocity;
			EscVel.Z = FFixedPoint::Zero;
			const FFixedPoint EscFloor = GetDefault<USeinARTSCoreSettings>()->AvoidanceMovingSpeedFloor;
			if (EscVel.SizeSquared() <= EscFloor * EscFloor)
			{
				EscapeHoldTime = EscapeHoldTime + DeltaTime;
			}
			else
			{
				EscapeHoldTime = FFixedPoint::Zero;
			}
			if (EscapeHoldTime >= FFixedPoint::FromInt(3) / FFixedPoint::FromInt(5)) // 0.6s
			{
				bExitEscape = true;
				bAttemptFailed = true;
				EscOutcome = TEXT("held");
			}
		}

		if (bExitEscape)
		{
			if (bAttemptFailed) { ++EscapeAttempts; }
			else { EscapeAttempts = 0; HoldTime = FFixedPoint::Zero; bStage1Fired = false; }
			// Restore identically on success and failure: drop the escape path
			// and resume through the EXISTING first-resolve machinery (correct
			// Throttled wait-and-retry for free; OnMoveBegin re-fires — accepted
			// as the per-order reset running after a detour).
			bEscapeMode = false;
			EscapeHoldTime = FFixedPoint::Zero;
			NextEscalationAt = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);
			Path.Clear();
			CurrentWaypointIndex = 0;
			bPathResolved = false;
			// The resume's first-resolve commits a fresh path — reset the repath
			// clock like every other path commit, or a frozen near-due timer
			// fires a second, identical request right behind it (budget waste).
			TimeSinceLastRepath = FFixedPoint::Zero;
			// The escape leg corrupted the near-goal progress high-water —
			// a resumed approach must start a fresh window.
			BestDistToFinalSq = FFixedPoint::FromInt(1000000);
			TimeStalledNearGoal = FFixedPoint::Zero;
#if !UE_BUILD_SHIPPING
			UE_LOG(LogSeinMoveTrace, Verbose,
				TEXT("[ESC] t=%d h=%d:%d escape-%s attempts=%d"),
				World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation,
				EscOutcome, EscapeAttempts);
#endif
			if (EscapeAttempts >= 3)
			{
#if !UE_BUILD_SHIPPING
				UE_LOG(LogSeinMoveTrace, Verbose, TEXT("[ESC] t=%d h=%d:%d STRANDED"),
					World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation);
#endif
				Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
				return true;
			}
		}
		else
		{
			// AnimBPs must not blend braking anims against a wall mid-escape.
			MoveComp->bArrivalImminent = false;
		}
		return false;
	}

	// Under-reports if the movement consumed multiple waypoints in one tick
	// (only the latest advance fires the notify). Acceptable for MVP; if
	// per-step granularity ever matters, swap to a TFunctionRef callback.
	if (CurrentWaypointIndex > PrevWaypoint)
	{
		NotifyWaypointReached(CurrentWaypointIndex - 1, Path.Waypoints.Num());
	}

	// Update bArrivalImminent — true while in the kinematic brake zone of
	// the final waypoint (MaxArrivalSpeed cap < cruise TopSpeed). AnimBPs
	// read this via FSeinMovementComponent::bArrivalImminent to blend into
	// arrival/braking animations without re-deriving from speed deltas.
	// Reset to false on arrival; OnCancel/OnFail also clear it.
	if (!bReachedEnd && Path.Waypoints.Num() > 0)
	{
		const FFixedVector AgentPos = Entity->Transform.GetLocation();
		const FFixedVector FinalWp = Path.Waypoints.Last();
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinal = ToFinal.Size();
		// The braking rate is per-mode now (Movement+ UDS), so query it through the movement instead
		// of the bare component. Ultra-basic modes return 0 → KinematicArrivalSpeedCap gives a huge
		// cap → bArrivalImminent stays false (they don't brake), which is correct.
		const FFixedPoint MaxArrivalSpeed = USeinMovement::KinematicArrivalSpeedCap(
			DistFinal, Movement->GetDeceleration(MoveComp));
		MoveComp->bArrivalImminent = MaxArrivalSpeed < MoveComp->TopSpeed;
	}
	else
	{
		MoveComp->bArrivalImminent = false;
	}

	// Near-goal failsafe. A unit pinned within a tight band of a final waypoint it can't physically
	// occupy (a nav-reachable cell whose body footprint is wall/crowd-blocked) never satisfies the
	// harness arrival — it is never within AcceptanceRadius, and it heads INTO the obstacle so the
	// overshoot guard (which needs "heading away") won't fire either. Left alone it would push
	// forever. So: once it stops closing for a short while THIS close, this is as near as its body
	// fits — arrive. Final leg only, and the band is TIGHT — max(3× acceptance, footprint + 100cm,
	// see StallBandSq) — so only a unit essentially AT its goal (as near as its BODY allows) settles,
	// never one still approaching (that was the old crowd-aware band's "forgotten units" bug). The
	// body-aware floor keeps goal-flush wall pins OURS rather than the hold-escape ladder's — the
	// ladder escaping a unit that is simply as-close-as-it-fits would oscillate forever.
	if (!bReachedEnd && Path.Waypoints.Num() > 0
		&& CurrentWaypointIndex >= Path.Waypoints.Num() - 1)
	{
		const FFixedVector AgentPos = Entity->Transform.GetLocation();
		FFixedVector ToFinal = Path.Waypoints.Last() - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinalSq = ToFinal.SizeSquared();
		const FFixedPoint VicinitySq = StallBandSq;

		if (DistFinalSq > VicinitySq)
		{
			// Outside the tight band — re-arm the progress high-water + clock for a fresh approach.
			BestDistToFinalSq = DistFinalSq;
			TimeStalledNearGoal = FFixedPoint::Zero;
		}
		else
		{
			// Clamp the high-water up on the first in-band tick (it inits to a large sentinel).
			if (DistFinalSq > BestDistToFinalSq) BestDistToFinalSq = DistFinalSq;
			// Meaningful-closing test in ACTUAL distance (a squared additive epsilon vanishes at
			// range). 10 cm: larger than collision jitter, far smaller than a genuine approach.
			const FFixedPoint DistFinal = SeinMath::Sqrt(DistFinalSq);
			const FFixedPoint BestDist  = SeinMath::Sqrt(BestDistToFinalSq);
			if (DistFinal + FFixedPoint::FromInt(10) < BestDist)
			{
				BestDistToFinalSq = DistFinalSq;
				TimeStalledNearGoal = FFixedPoint::Zero;
			}
			else
			{
				TimeStalledNearGoal = TimeStalledNearGoal + DeltaTime;
				// 0.75 s pinned this close → this is as far in as the body gets; settle.
				// Routed through the mode's arrival policy (not a raw Velocity=0) so a
				// crowd-stall arrival leaves the unit in the same per-class state as a
				// clean ring arrival — both arrival owners share one stop semantics.
				if (TimeStalledNearGoal >= FFixedPoint::FromInt(3) / FFixedPoint::FromInt(4))
				{
#if !UE_BUILD_SHIPPING
					// Movement-trace event: the third arrival owner (crowd-stall settle).
					UE_LOG(LogSeinMoveTrace, Verbose,
						TEXT("[ARRIVE] t=%d h=%d:%d cause=stall dist=%.0f accept=%.0f"),
						World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation,
						DistFinal.ToFloat(), SeinMath::Sqrt(AcceptanceRadiusSq).ToFloat());
#endif
					Movement->DispatchArrivalMotion(TickCtx);
					bReachedEnd = true;
				}
			}
		}
	}

	// HOLD-ESCAPE LADDER — the far-from-goal counterpart of the stall failsafe
	// above (see the state block in the header). Detects a commanded unit whose
	// APPLIED step is ~zero mid-route: a wall face-pin has NO other exit — the
	// failsafe above is final-leg-only and repaths keep succeeding — so left
	// alone it stands forever (the straggler). Escalation is gated on a
	// footprint probe of the commanded direction, so policy zeros (pivots,
	// yields) never trigger anything; normal movement never reaches any of
	// this by construction. Tier-1 harness modes only: Tier-2 vehicle Ticks
	// persist COMMANDED velocity, so they never read held here (they carry
	// their own reverse-unstick machinery).
	if (!bReachedEnd && Path.Waypoints.Num() > 0)
	{
		const FFixedPoint HoldFloor = GetDefault<USeinARTSCoreSettings>()->AvoidanceMovingSpeedFloor;
		FFixedVector HeldVel = MoveComp->Velocity;
		HeldVel.Z = FFixedPoint::Zero;
		bool bHeld = HeldVel.SizeSquared() <= HoldFloor * HoldFloor;
		// Exclude the stall failsafe's exact accrual domain — the CONJUNCTION of
		// final-leg AND the shared body-aware settle band (final-leg alone would
		// be wrong: LoS smoothing makes the final leg cover most of a route).
		if (bHeld && CurrentWaypointIndex >= Path.Waypoints.Num() - 1)
		{
			FFixedVector HeldToFinal = Path.Waypoints.Last() - Entity->Transform.GetLocation();
			HeldToFinal.Z = FFixedPoint::Zero;
			if (HeldToFinal.SizeSquared() <= StallBandSq)
			{
				bHeld = false; // the stall failsafe owns near-goal stops
			}
		}

		if (!bHeld)
		{
			HoldTime = FFixedPoint::Zero;
			NextEscalationAt = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);
			bStage1Fired = false;
			// Genuine applied motion ends the stuck EPISODE — the consecutive
			// exhaustion counter starts fresh at the next one. (TotalEscapeEntries
			// deliberately does NOT reset — it is the per-order oscillation cap.)
			EscapeAttempts = 0;
		}
		else
		{
			// NOTE: a successful repath / Path swap deliberately does NOT reset
			// this clock — repaths SUCCEED every interval in the pinned state
			// (that is the pathology); only genuine applied motion clears it.
			HoldTime = HoldTime + DeltaTime;
			if (HoldTime >= NextEscalationAt)
			{
				NextEscalationAt = NextEscalationAt + FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);

				// MECHANICAL-BLOCK PROBE (boundaries only, never per-tick): is
				// the direction the unit is trying to go footprint-refused? A
				// PASSABLE probe means the zero command is the mode's own policy
				// (pivot-in-place, yield) — keep accruing, re-check at the next
				// boundary, escalate nothing.
				const FFixedVector AgentPos = Entity->Transform.GetLocation();
				const FFixedPoint FootR = FootprintRadius;
				FFixedVector ToWp = Path.Waypoints[CurrentWaypointIndex] - AgentPos;
				ToWp.Z = FFixedPoint::Zero;
				const FFixedPoint ToWpLen = ToWp.Size();
				bool bBlocked = false;
				if (ToWpLen > FFixedPoint::Epsilon && Nav)
				{
					const FFixedPoint ProbeDist = FootR + FFixedPoint::FromInt(50);
					const FFixedVector Probe(
						AgentPos.X + (ToWp.X / ToWpLen) * ProbeDist,
						AgentPos.Y + (ToWp.Y / ToWpLen) * ProbeDist,
						AgentPos.Z);
					bBlocked = !Movement->IsFootprintPassable(Probe, Nav);
				}

				if (bBlocked && !bStage1Fired)
				{
					// STAGE 1: force a repath through the existing machinery
					// (fires next tick's repath block, before the movement tick).
					// Cheap; cures stale-carrot variants. Its real value is
					// OffPathOnly mode, whose drift gate never rescues an
					// on-polyline pinned unit.
					bStage1Fired = true;
					bForceRepathNow = true;
#if !UE_BUILD_SHIPPING
					UE_LOG(LogSeinMoveTrace, Verbose,
						TEXT("[ESC] t=%d h=%d:%d stage1 forced-repath held=%.2fs"),
						World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation,
						HoldTime.ToFloat());
#endif
				}
				else if (bBlocked)
				{
					// PER-ORDER OSCILLATION CAP: a reproducible pin whose escapes
					// SUCCEED never accumulates the consecutive counter — the
					// resume re-plans into the same pin and the cycle would run
					// forever (walk → pin → escape → walk back). Five installed
					// escape legs in one order is that cycle, not recovery.
					if (TotalEscapeEntries >= 5)
					{
#if !UE_BUILD_SHIPPING
						UE_LOG(LogSeinMoveTrace, Verbose,
							TEXT("[ESC] t=%d h=%d:%d STRANDED (escape budget spent: %d entries)"),
							World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation,
							TotalEscapeEntries);
#endif
						Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
						return true;
					}
					// STAGE 2: ask the nav for an escape target and walk there
					// as a short internal leg. Every no-answer consumes an
					// exhaustion slot so the ladder terminates on ANY nav.
					FFixedVector Target = FFixedVector::ZeroVector;
					bool bGotTarget = false;
					if (Nav)
					{
						FSeinEscapeQuery EscQ;
						EscQ.From = AgentPos;
						EscQ.Requester = OwnerEntity;
						EscQ.AgentNavLayerMask = NavComp ? NavComp->NavLayerMask : 0x01;
						EscQ.AgentFootprintRadius = FootR;
						// BlockedTerrainTags left empty — matches what this
						// action's own path requests carry today.
						bGotTarget = Nav->QueryEscapeTarget(EscQ, Target);
					}
					if (bGotTarget)
					{
						FFixedVector ToTarget = Target - AgentPos;
						ToTarget.Z = FFixedPoint::Zero;
						const FFixedPoint EntryDist = ToTarget.Size();
						// Escape acceptance = max(50, min(footprint, EntryDist/3)),
						// and the ENTRY GATE: the target must sit decisively beyond
						// the overshoot guard's 2x-acceptance vicinity, or the leg
						// instant-arrives with zero motion (the guard passes on the
						// entry state: velocity ~0, facing the wall) and the ladder
						// would cycle forever without exhausting.
						FFixedPoint AccEsc = EntryDist / FFixedPoint::FromInt(3);
						if (FootR > FFixedPoint::Zero && AccEsc > FootR) { AccEsc = FootR; }
						if (AccEsc < FFixedPoint::FromInt(50)) { AccEsc = FFixedPoint::FromInt(50); }
						const FFixedPoint MinEntry =
							AccEsc * FFixedPoint::FromInt(2) + MoveComp->TopSpeed * DeltaTime;
						if (EntryDist <= MinEntry)
						{
							bGotTarget = false;
						}
						else
						{
							// Install the escape leg: [AgentPos → Target] — the
							// straight-line-path template. Never a single waypoint
							// (zero segments; the harness indexes
							// Waypoints[CurrentWaypointIndex] unguarded).
							Path.Clear();
							Path.Waypoints.Add(AgentPos);
							Path.Waypoints.Add(Target);
							Path.bIsValid = true;
							Path.bIsPartial = false;
							Path.DeriveSegmentsFromWaypoints();
							CurrentWaypointIndex = 0;
							bEscapeMode = true;
							++TotalEscapeEntries;
							EscapeTarget = Target;
							EscapeAcceptSq = AccEsc * AccEsc;
							EscapeHoldTime = FFixedPoint::Zero;
							// The escape leg corrupts the near-goal progress
							// high-water; re-arm for the resumed approach.
							BestDistToFinalSq = FFixedPoint::FromInt(1000000);
							TimeStalledNearGoal = FFixedPoint::Zero;
#if !UE_BUILD_SHIPPING
							UE_LOG(LogSeinMoveTrace, Verbose,
								TEXT("[ESC] t=%d h=%d:%d stage2 target=(%.0f,%.0f) dist=%.0f acc=%.0f attempts=%d"),
								World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation,
								Target.X.ToFloat(), Target.Y.ToFloat(),
								EntryDist.ToFloat(), AccEsc.ToFloat(), EscapeAttempts);
#endif
						}
					}
					if (!bGotTarget && !bEscapeMode)
					{
						++EscapeAttempts;
#if !UE_BUILD_SHIPPING
						UE_LOG(LogSeinMoveTrace, Verbose,
							TEXT("[ESC] t=%d h=%d:%d stage2 no-answer attempts=%d"),
							World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation,
							EscapeAttempts);
#endif
						if (EscapeAttempts >= 3)
						{
#if !UE_BUILD_SHIPPING
							UE_LOG(LogSeinMoveTrace, Verbose, TEXT("[ESC] t=%d h=%d:%d STRANDED"),
								World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation);
#endif
							Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
							return true;
						}
					}
				}
			}
		}
	}

	if (bReachedEnd)
	{
		// Terminalize before either OnMoveEnd or the proxy delegate: both may
		// synchronously call EndAbility, whose latent-action cancellation must
		// observe this action as already complete.
		Complete();
		FinalizeMovementOnce();
		NotifyCompleted();
		return true;
	}
	return false;
}

void USeinMoveToAction::OnCancel()
{
	FinalizeMovementOnce();
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyCancelled();
	}
}

void USeinMoveToAction::OnFail(uint8 ReasonCode)
{
	FinalizeMovementOnce();
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyFailed(static_cast<ESeinMoveFailureReason>(ReasonCode));
	}
}

void USeinMoveToAction::OnTimelineAbandoned()
{
	// An abandoned timeline is neither a cancellation nor a move end. In
	// particular, do not call FinalizeMovementOnce(): OnMoveEnd may execute
	// Blueprint and would manufacture gameplay on the discarded timeline.
	USeinMoveToProxy* Proxy = Observer.Get();
	Observer.Reset();
	Movement = nullptr;
	bMovementFinalized = true;
	// Assignment from a fresh value releases array capacity too. Path::Clear()
	// retains slack, which would let an externally-retained abandoned action
	// pin a large route allocation across repeated editor/module reloads.
	Path = FSeinPath();
	OwningAbility = nullptr;
	OwnerEntity = FSeinEntityHandle::Invalid();
	if (Proxy)
	{
		Proxy->AbandonForSnapshotRestore();
	}
}

void USeinMoveToAction::FinalizeMovementOnce()
{
	if (bMovementFinalized) return;
	bMovementFinalized = true;

	// Release the borrowed reference before the Blueprint-capable end hook so
	// synchronous re-entry cannot dispatch it twice.
	USeinMovement* EndingMovement = Movement;
	Movement = nullptr;

	if (!OwningAbility) return;
	UWorld* World = OwningAbility->GetWorld();
	if (!World) return;
	// Cancel before any Blueprint-capable end hook. OnMoveEnd may synchronously
	// issue a new order for this entity; cancelling afterward would erase that
	// new order's continuation instead of the one owned by this action.
	if (USeinNavigationSubsystem* NavigationSub =
		World->GetSubsystem<USeinNavigationSubsystem>())
	{
		NavigationSub->CancelPathRequest(OwnerEntity);
	}
	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	if (EndingMovement)
	{
		if (USeinMovementSubsystem* MovementSub =
			World->GetSubsystem<USeinMovementSubsystem>())
		{
			MovementSub->MarkMovementStateDirty(OwnerEntity);
		}
		if (FSeinEntity* Entity =
			Sim->GetEntityMutable(OwnerEntity))
		{
			EndingMovement->OnMoveEnd(*Entity);
		}
	}

	FSeinMovementComponent* MoveComp =
		Sim->GetComponentMutable<FSeinMovementComponent>(
			OwnerEntity);
	if (!MoveComp) return;
	MoveComp->bArrivalImminent = false;
	// Mirror the action lifecycle: action no longer driving the entity →
	// "input released" signal goes false. AnimBPs reading bHasMovementInput
	// blend out of locomotion immediately, instead of waiting for Velocity
	// to coast through the deceleration curve.
	MoveComp->bHasTarget = false;
}

void USeinMoveToAction::NotifyCompleted()
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyCompleted();
	}
}

void USeinMoveToAction::NotifyWaypointReached(int32 Index, int32 Total)
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyWaypointReached(Index, Total);
	}
}

void USeinMoveToAction::NotifyPartialPath()
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyPartialPath();
	}
}

void USeinMoveToAction::NotifyPathRecomputed()
{
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyPathRecomputed();
	}
}
