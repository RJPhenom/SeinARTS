/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.cpp
 */

#include "Actions/SeinMoveToAction.h"
#include "Abilities/SeinMoveToProxy.h"
#include "SeinNavigation.h"
#include "SeinNavigationAStar.h"  // Cast for escape-nudge fallback
#include "SeinNavigationSubsystem.h"
#include "Movement/SeinMovement.h"
#include "Movement/SeinBasicMovement.h"

#include "Abilities/SeinAbility.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMove, Log, All);

namespace
{
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
	bInEscapeMode = false;
	EscapeTimer = FFixedPoint::Zero;
	EscapeStartPos = FFixedVector::ZeroVector;
	StallVicinityRadiusSq = FFixedPoint::Zero;
	BestDistToFinalSq = FFixedPoint::FromInt(1000000);
	TimeStalledNearGoal = FFixedPoint::Zero;
	Path.Clear();
	Movement = nullptr;
}

bool USeinMoveToAction::TickAction(FFixedPoint DeltaTime, USeinWorldSubsystem& World)
{
	FSeinEntity* Entity = World.GetEntity(OwnerEntity);
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
	FSeinMovementComponent* MoveComp = World.GetComponent<FSeinMovementComponent>(OwnerEntity);
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

	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(&World);
	USeinNavigationSubsystem* NavSub = World.GetWorld()
		? World.GetWorld()->GetSubsystem<USeinNavigationSubsystem>()
		: nullptr;

	// First-tick setup: instantiate the movement, then either FindPath or
	// synthesize a straight-line path depending on the movement's
	// `BypassPathfinding()` answer. Movement comes first so we can ask it.
	if (!bPathResolved)
	{
		// Resolve the movement class. Soft class path on FSeinMovementComponent
		// decouples CoreEntity from SeinARTSMovement (where the movement
		// classes live); TryLoadClass pulls in the resolved UClass* at first use.
		UClass* MovementClass = MoveComp->MovementClass.IsValid()
			? MoveComp->MovementClass.TryLoadClass<USeinMovement>()
			: nullptr;
		if (!MovementClass || MovementClass->HasAnyClassFlags(CLASS_Abstract))
		{
			MovementClass = USeinBasicMovement::StaticClass();
		}
		Movement = NewObject<USeinMovement>(this, MovementClass);
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
		const ESeinPathResult Result = Movement->PlanPath(PlanCtx, Path);

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
		// return false WITHOUT setting bPathResolved or keeping Movement so
		// the action keeps ticking until budget frees up.
		if (Result == ESeinPathResult::Throttled)
		{
			Movement = nullptr;
			return false;
		}
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
			UE_LOG(LogSeinMove, Warning,
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
			: FFixedPoint::FromInt(50);
		AcceptanceRadiusSq = Acceptance * Acceptance;

		// Near-goal stall-settle vicinity (see StallVicinityRadiusSq in the
		// header). Sized to absorb footprint-pinning: a body of radius R held off
		// a wall-embedded goal can never get its center closer than ~R, so the
		// band must reach past the acceptance ring by a couple of footprints to
		// recognise "this is as close as the body fits." Uses the SAME footprint
		// cascade as runtime collision + path planning (Extents → NavComp → 0).
		// Floored at 2×Acceptance so zero-footprint (intangible) units still get a
		// meaningful band — harmless there since a point body never pins.
		const FFixedPoint StallFootprint =
			USeinMovement::ResolveCollisionRadius(&World, OwnerEntity, NavComp);
		FFixedPoint StallVicinityRadius = Acceptance + StallFootprint * FFixedPoint::Two;
		const FFixedPoint MinStallVicinity = Acceptance * FFixedPoint::Two;
		if (StallVicinityRadius < MinStallVicinity) StallVicinityRadius = MinStallVicinity;
		StallVicinityRadiusSq = StallVicinityRadius * StallVicinityRadius;

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
	// Escape mode also skips repath entirely — the escape-nudge fallback
	// below owns Path while we're trying to nudge out of a stuck cell. It
	// runs its own repath attempts at a different cadence (gated on
	// distance moved since escape entry) and exits escape on success.
	if (bInEscapeMode)
	{
		// Skip — escape-nudge logic below replaces Path each tick until
		// either the chassis escapes (fresh repath returns a valid full
		// path) or the escape timer expires (Stranded).
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
	const ESeinRepathMode RepathMode = NavComp ? NavComp->RepathMode : ESeinRepathMode::Interval;
	const FFixedPoint RepathInterval = (NavComp && NavComp->RepathInterval > FFixedPoint::Zero)
		? NavComp->RepathInterval : FFixedPoint::FromInt(1) / FFixedPoint::FromInt(4);
	const int32 RepathFailureLimit = (NavComp && NavComp->RepathFailureLimit > 0)
		? NavComp->RepathFailureLimit : 3;
	const FFixedPoint OffPathThreshold = (NavComp && NavComp->OffPathThreshold > FFixedPoint::Zero)
		? NavComp->OffPathThreshold : FFixedPoint::FromInt(75);

	if (!NavComp)
	{
		// No nav component — skip the repath block entirely. The initial
		// path remains in force; if the agent drifts, no recovery happens.
		// Same fall-through path as the bypass-pathfinding branch.
	}
	else
	switch (RepathMode)
	{
	case ESeinRepathMode::Interval:
	{
		const FFixedPoint Interval = RepathInterval;
		if (TimeSinceLastRepath >= Interval && Nav && NavSub)
		{
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
				if (Path.bIsPartial) NotifyPartialPath();
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
				const int32 Limit = RepathFailureLimit;
				UE_LOG(LogSeinMove, Verbose,
					TEXT("Repath (Interval) failed: attempt %d/%d (entity %s)"),
					ConsecutiveRepathFailures, Limit, *OwnerEntity.ToString());
				if (ConsecutiveRepathFailures >= Limit)
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
		if (TimeSinceLastRepath < MinAttemptInterval) break;
		if (!Nav) break;

		const FFixedVector AgentPos = Entity->Transform.GetLocation();
		const FFixedPoint DriftSq = OffPathMinDistSqToPolyline(AgentPos, PathOriginAgentPos, Path.Waypoints);

		// Threshold fallback: 75cm if the field was zeroed out (e.g.,
		// legacy data pre-dating this field, or designer set it to 0
		// expecting "use default"). Matches the constructor default.
		const FFixedPoint Threshold = OffPathThreshold;
		const FFixedPoint ThresholdSq = Threshold * Threshold;

		if (DriftSq <= ThresholdSq) break;
		if (!NavSub) break;

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
				Threshold.ToFloat(),
				NewPath.Waypoints.Num(),
				AgentPos.X.ToFloat(),
				AgentPos.Y.ToFloat(),
				Path.bIsPartial ? TEXT(" [PARTIAL]") : TEXT(""));
			if (Path.bIsPartial) NotifyPartialPath();
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
	} // end else (non-bypass repath block)

	// ----------------------------------------------------------------------
	// Escape-nudge fallback. When A* can't expand from the chassis's start
	// cell, FindPath returns a partial path with a single waypoint at (or
	// very near) the chassis itself — the chassis no-ops and the action
	// would otherwise instantly NotifyCompleted (the "0 drivable segments"
	// warning case). Same trap fires when ConsecutiveRepathFailures climbs
	// (path system gives up trying because every recent repath returned
	// degenerate or failed). Either way: rather than let the move silently
	// end, drive the chassis up the WallDistance gradient (toward the
	// highest-WD passable neighbor) until either A* can plan again or we
	// conclude the chassis is genuinely Stranded.
	//
	// Detection runs unconditionally (after both bypass-pathfinding and
	// normal repath branches). While in escape mode the override below
	// replaces Path each tick; the normal repath block above is gated to
	// skip while escape is active.
	// ----------------------------------------------------------------------
	{
		const FFixedVector AgentPosForEscape = Entity->Transform.GetLocation();
		const bool bDegeneratePath = Path.bIsValid
			&& Path.bIsPartial
			&& Path.Waypoints.Num() <= 1;

		if (!bInEscapeMode && (bDegeneratePath || ConsecutiveRepathFailures >= 2))
		{
			bInEscapeMode = true;
			EscapeTimer = FFixedPoint::Zero;
			EscapeStartPos = AgentPosForEscape;
			UE_LOG(LogSeinMove, Log,
				TEXT("MoveAction entering escape mode: agent=(%.1f,%.1f) degeneratePath=%d failures=%d dest=(%.1f,%.1f) (entity %s)"),
				AgentPosForEscape.X.ToFloat(), AgentPosForEscape.Y.ToFloat(),
				bDegeneratePath ? 1 : 0, ConsecutiveRepathFailures,
				Destination.X.ToFloat(), Destination.Y.ToFloat(),
				*OwnerEntity.ToString());
		}

		if (bInEscapeMode)
		{
			EscapeTimer = EscapeTimer + DeltaTime;
			// 2 seconds max — chassis at MoveSpeed should clear several cells
			// in that time. If it hasn't, we're genuinely stuck.
			const FFixedPoint MaxEscapeDuration = FFixedPoint::FromInt(2);

			bool bExitedEscape = false;

			// Re-attempt full PlanPath once the chassis has moved meaningfully
			// since entering escape (typical signal that the nudge worked and
			// we're back in plannable territory). Half-cell distance²
			// threshold so we don't fire on every tiny jitter.
			const FFixedPoint MinMoveToRetrySq = FFixedPoint::FromInt(50 * 50);
			if (NavSub && Movement
				&& (AgentPosForEscape - EscapeStartPos).SizeSquared() >= MinMoveToRetrySq)
			{
				FSeinPlanPathContext EscapeReplanCtx{
					*Entity, MoveComp, NavComp, Destination, Nav, NavSub, OwnerEntity, &World
				};
				FSeinPath NewPath;
				const ESeinPathResult Result = Movement->PlanPath(EscapeReplanCtx, NewPath);
				if (Result == ESeinPathResult::Found
					&& NewPath.bIsValid
					&& !NewPath.bIsPartial
					&& NewPath.Waypoints.Num() > 0)
				{
					// Escaped — use the fresh full path, resume normal driving.
					Path = NewPath;
					CurrentWaypointIndex = 0;
					PathOriginAgentPos = AgentPosForEscape;
					bInEscapeMode = false;
					EscapeTimer = FFixedPoint::Zero;
					ConsecutiveRepathFailures = 0;
					bExitedEscape = true;
					UE_LOG(LogSeinMove, Log,
						TEXT("MoveAction escape successful: repath returned %d waypoints (entity %s)"),
						Path.Waypoints.Num(), *OwnerEntity.ToString());
				}
			}

			if (!bExitedEscape)
			{
				// Timeout — chassis tried to escape, but didn't make enough
				// progress. Fail with Stranded so AI scripts can distinguish
				// "couldn't plan at all" (PathNotFound) from "tried to nudge
				// out of a stuck cell and failed."
				if (EscapeTimer >= MaxEscapeDuration)
				{
					UE_LOG(LogSeinMove, Warning,
						TEXT("MoveAction escape: timed out after %.2fs without exit (entity %s)"),
						EscapeTimer.ToFloat(), *OwnerEntity.ToString());
					Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
					return true;
				}

				// Find a nudge target. Requires the AStar nav impl — pluggable
				// nav providers that don't ship a FindEscapeNudgeTarget go
				// straight to Stranded.
				USeinNavigationAStar* NavAStar = Cast<USeinNavigationAStar>(Nav);
				if (!NavAStar)
				{
					UE_LOG(LogSeinMove, Warning,
						TEXT("MoveAction escape: Nav is not USeinNavigationAStar — Stranded (entity %s)"),
						*OwnerEntity.ToString());
					Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
					return true;
				}

				FFixedVector EscapeTarget;
				int32 TargetWD = -1;
				if (!NavAStar->FindEscapeNudgeTarget(AgentPosForEscape, EscapeTarget, TargetWD))
				{
					// No passable neighbor at chassis cell — sealed pocket.
					// No amount of nudging will help; fail immediately.
					UE_LOG(LogSeinMove, Warning,
						TEXT("MoveAction escape: no passable neighbor at chassis cell — Stranded (entity %s)"),
						*OwnerEntity.ToString());
					Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
					return true;
				}

				// Override Path with the escape target. The normal carrot
				// resolver + steering pipeline drives toward it next tick;
				// no changes needed in any movement subclass.
				Path.Clear();
				Path.Waypoints.Add(EscapeTarget);
				Path.bIsValid = true;
				Path.bIsPartial = false;
				Path.DeriveSegmentsFromWaypoints();
				CurrentWaypointIndex = 0;
				PathOriginAgentPos = AgentPosForEscape;
			}
		}
	}

	// Delegate the per-tick advance. Movement mutates Entity.Transform and
	// CurrentWaypointIndex; returns true when the final waypoint is reached.
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
	TickCtx.bAuthoritativeDestination = bAuthoritativeDestination;
	// Non-const: the near-goal stall-settle below can promote this to true to
	// force arrival when the unit is pinned short of an unreachable goal.
	bool bReachedEnd = Movement->Tick(TickCtx);

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
		const FFixedPoint MaxArrivalSpeed = USeinMovement::KinematicArrivalSpeedCap(
			DistFinal, MoveComp->Deceleration);
		MoveComp->bArrivalImminent = MaxArrivalSpeed < MoveComp->TopSpeed;
	}
	else
	{
		MoveComp->bArrivalImminent = false;
	}

	// ----------------------------------------------------------------------
	// Near-goal stall settle. See StallVicinityRadiusSq in the header. A unit
	// pinned a footprint short of a final waypoint it can't physically occupy
	// (nav-reachable cell, collision-blocked body) satisfies neither the
	// movement's within-acceptance nor its overshoot arrival, so it would seek
	// — and, for face-velocity movements, SPIN — that point forever. Convert
	// "near the goal and unable to get any closer for a while" into arrival.
	// Skipped while escaping (that path owns Path and aims at a nudge cell, not
	// the goal) and once the movement already reported arrival this tick.
	// ----------------------------------------------------------------------
	if (!bReachedEnd && !bInEscapeMode && Path.Waypoints.Num() > 0
		&& StallVicinityRadiusSq > FFixedPoint::Zero)
	{
		const FFixedVector AgentPos = Entity->Transform.GetLocation();
		FFixedVector ToFinal = Path.Waypoints.Last() - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinalSq = ToFinal.SizeSquared();

		if (DistFinalSq > StallVicinityRadiusSq)
		{
			// Outside the near-goal band — re-arm the high-water mark to here so a
			// fresh approach (after a detour / repath / escape that moved us away)
			// measures progress from this point, not a stale earlier best.
			BestDistToFinalSq = DistFinalSq;
			TimeStalledNearGoal = FFixedPoint::Zero;
		}
		else
		{
			// Within the band. BestDistToFinalSq only DECREASES here, so jitter or
			// orbit around the closest reachable point never resets the clock —
			// only genuine fresh closing does. ProgressEpsilon keeps fixed-point
			// noise from registering as progress.
			const FFixedPoint ProgressEpsilonSq = FFixedPoint::FromInt(4); // ~(2cm)²
			if (DistFinalSq + ProgressEpsilonSq < BestDistToFinalSq)
			{
				BestDistToFinalSq = DistFinalSq;
				TimeStalledNearGoal = FFixedPoint::Zero;
			}
			else
			{
				TimeStalledNearGoal = TimeStalledNearGoal + DeltaTime;
				// 0.75s — long enough that legitimate near-goal maneuvering (rounding
				// a neighbour, a brief brake) keeps closing and resets the clock;
				// short enough that a true pin stops spinning promptly.
				const FFixedPoint StallSettleDuration =
					FFixedPoint::FromInt(3) / FFixedPoint::FromInt(4);
				if (TimeStalledNearGoal >= StallSettleDuration)
				{
					UE_LOG(LogSeinMove, Log,
						TEXT("MoveAction near-goal settle: pinned %.1fcm from final for "
						     "%.2fs — arriving (entity %s)"),
						SeinMath::Sqrt(DistFinalSq).ToFloat(),
						TimeStalledNearGoal.ToFloat(),
						*OwnerEntity.ToString());
					MoveComp->Velocity = FFixedVector::ZeroVector;
					bReachedEnd = true;
				}
			}
		}
	}

	if (bReachedEnd)
	{
		Movement->OnMoveEnd(*Entity);
		// Centralized end-of-action cleanup so success and cancel/fail paths
		// agree on transient state (bHasTarget, bArrivalImminent). Reads
		// MoveComp through the same chain ResetTransientMoveState uses; the
		// re-lookup is cheap and keeps cleanup logic in one place.
		ResetTransientMoveState();
		NotifyCompleted();
		Complete();
		return true;
	}
	return false;
}

void USeinMoveToAction::OnCancel()
{
	ResetTransientMoveState();
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyCancelled();
	}
}

void USeinMoveToAction::OnFail(uint8 ReasonCode)
{
	ResetTransientMoveState();
	if (USeinMoveToProxy* Proxy = Observer.Get())
	{
		Proxy->NotifyFailed(static_cast<ESeinMoveFailureReason>(ReasonCode));
	}
}

void USeinMoveToAction::ResetTransientMoveState()
{
	// Walks OwningAbility → World → SimSubsystem → MovementComponent. Any
	// link in that chain being null is a routine "we're tearing down" state
	// — fail silently rather than warn.
	if (!OwningAbility) return;
	UWorld* World = OwningAbility->GetWorld();
	if (!World) return;
	USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;
	FSeinMovementComponent* MoveComp = Sim->GetComponent<FSeinMovementComponent>(OwnerEntity);
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
