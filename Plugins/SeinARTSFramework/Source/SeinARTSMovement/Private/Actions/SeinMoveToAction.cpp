/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoveToAction.cpp
 */

#include "Actions/SeinMoveToAction.h"
#include "Testing/SeinMoveToActionContinuationTestAccess.h"
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
#include "Math/BigInt.h"
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

	FFixedPoint SaturatingPositiveAdd(FFixedPoint A, FFixedPoint B)
	{
		if (A > FFixedPoint::Zero && B > FFixedPoint::Zero
			&& A > FFixedPoint::MaxValue - B)
		{
			return FFixedPoint::MaxValue;
		}
		return A + B;
	}

	FFixedPoint SaturatingPositiveScale(FFixedPoint Value, int32 Scale)
	{
		const FFixedPoint FixedScale = FFixedPoint::FromInt(Scale);
		if (Value > FFixedPoint::Zero && Scale > 0
			&& Value > FFixedPoint::MaxValue / FixedScale)
		{
			return FFixedPoint::MaxValue;
		}
		return Value * FixedScale;
	}

	int512 MakeSignedInt512(int64 Value)
	{
		int512 Result(Value);
		if (Value < 0)
		{
			// TBigInt's int64 constructor initializes only its low two words.
			uint32* Words = Result.GetBits();
			for (int32 WordIndex = 2; WordIndex < 512 / 32; ++WordIndex)
			{
				Words[WordIndex] = MAX_uint32;
			}
		}
		return Result;
	}

	bool IsPlanarCrossExactlyZero(
		const FFixedVector& Q,
		const FFixedVector& S,
		const FFixedVector& E)
	{
		const int64 QX = (Q.X - S.X).Value;
		const int64 QY = (Q.Y - S.Y).Value;
		const int64 DX = (E.X - S.X).Value;
		const int64 DY = (E.Y - S.Y).Value;
#if defined(__GNUC__) || defined(__clang__)
		return static_cast<__int128>(QX) * DY
			== static_cast<__int128>(QY) * DX;
#elif defined(_MSC_VER)
		int64 LeftHigh = 0;
		int64 RightHigh = 0;
		const int64 LeftLow = _mul128(QX, DY, &LeftHigh);
		const int64 RightLow = _mul128(QY, DX, &RightHigh);
		return LeftHigh == RightHigh && LeftLow == RightLow;
#else
#error "Platform does not support 128-bit multiplication"
#endif
	}

	/** Exact raw-value fallback for spans whose fixed-point normalization
	 *  saturates or quantizes away a small perpendicular offset. */
	bool IsPointWithinSegmentDistanceXYExact(
		const FFixedVector& Q,
		const FFixedVector& S,
		const FFixedVector& E,
		FFixedPoint Radius)
	{
		const int512 DX = MakeSignedInt512(E.X.Value)
			- MakeSignedInt512(S.X.Value);
		const int512 DY = MakeSignedInt512(E.Y.Value)
			- MakeSignedInt512(S.Y.Value);
		const int512 QX = MakeSignedInt512(Q.X.Value)
			- MakeSignedInt512(S.X.Value);
		const int512 QY = MakeSignedInt512(Q.Y.Value)
			- MakeSignedInt512(S.Y.Value);
		const int512 RadiusRaw = MakeSignedInt512(Radius.Value);
		const int512 RadiusSquared = RadiusRaw * RadiusRaw;
		const int512 SegmentLengthSquared = DX * DX + DY * DY;
		const int512 QueryDistanceSquared = QX * QX + QY * QY;
		const int512 Zero(int64(0));
		if (SegmentLengthSquared == Zero)
		{
			return QueryDistanceSquared <= RadiusSquared;
		}

		const int512 Projection = QX * DX + QY * DY;
		if (Projection <= Zero)
		{
			return QueryDistanceSquared <= RadiusSquared;
		}
		if (Projection >= SegmentLengthSquared)
		{
			const int512 EX = MakeSignedInt512(Q.X.Value)
				- MakeSignedInt512(E.X.Value);
			const int512 EY = MakeSignedInt512(Q.Y.Value)
				- MakeSignedInt512(E.Y.Value);
			return EX * EX + EY * EY <= RadiusSquared;
		}

		const int512 Cross = QX * DY - QY * DX;
		return Cross * Cross
			<= RadiusSquared * SegmentLengthSquared;
	}

	/** Overflow-safe planar segment-distance predicate. Ordinary world spans
	 *  use normalized fixed-point math; saturation and quantization boundaries
	 *  fall back to an exact raw-value integer comparison. */
	bool IsPointWithinSegmentDistanceXY(
		const FFixedVector& Q,
		const FFixedVector& S,
		const FFixedVector& E,
		FFixedPoint Radius)
	{
		const FFixedVector PlanarS(S.X, S.Y, FFixedPoint::Zero);
		const FFixedVector PlanarE(E.X, E.Y, FFixedPoint::Zero);
		const FFixedVector PlanarQ(Q.X, Q.Y, FFixedPoint::Zero);
		if (Radius < FFixedPoint::Zero)
		{
			return false;
		}
		const FFixedPoint SegmentDistance =
			FFixedVector::DistanceSaturated(PlanarS, PlanarE);
		if (SegmentDistance == FFixedPoint::MaxValue)
		{
			return IsPointWithinSegmentDistanceXYExact(Q, S, E, Radius);
		}
		if (SegmentDistance <= FFixedPoint::Epsilon)
		{
			return FFixedVector::IsPlanarDistanceWithin(PlanarQ, PlanarS, Radius);
		}

		const FFixedPoint QueryDistance =
			FFixedVector::DistanceSaturated(PlanarS, PlanarQ);
		const FFixedPoint EndQueryDistance =
			FFixedVector::DistanceSaturated(PlanarE, PlanarQ);
		if (QueryDistance == FFixedPoint::MaxValue
			|| EndQueryDistance == FFixedPoint::MaxValue)
		{
			return IsPointWithinSegmentDistanceXYExact(Q, S, E, Radius);
		}
		const FFixedVector SegmentDirection =
			FFixedVector::GetSafeNormalDifference(PlanarS, PlanarE);
		if (QueryDistance <= FFixedPoint::Epsilon)
		{
			return true;
		}
		const FFixedVector QueryDirection =
			FFixedVector::GetSafeNormalDifference(PlanarS, PlanarQ);
		if (FFixedVector::DotProduct(
			QueryDirection, SegmentDirection) <= FFixedPoint::Zero)
		{
			return FFixedVector::IsPlanarDistanceWithin(PlanarQ, PlanarS, Radius);
		}
		const FFixedVector EndToQueryDirection =
			FFixedVector::GetSafeNormalDifference(PlanarE, PlanarQ);
		if (FFixedVector::DotProduct(
			EndToQueryDirection, SegmentDirection) >= FFixedPoint::Zero)
		{
			return FFixedVector::IsPlanarDistanceWithin(PlanarQ, PlanarE, Radius);
		}

		FFixedPoint Cross = QueryDirection.X * SegmentDirection.Y
			- QueryDirection.Y * SegmentDirection.X;
		if (Cross < FFixedPoint::Zero)
		{
			Cross = -Cross;
		}
		if (Cross <= FFixedPoint::Epsilon)
		{
			if (Cross == FFixedPoint::Zero
				&& IsPlanarCrossExactlyZero(PlanarQ, PlanarS, PlanarE))
			{
				return true;
			}
			return IsPointWithinSegmentDistanceXYExact(Q, S, E, Radius);
		}
		if (QueryDistance > FFixedPoint::MaxValue / Cross)
		{
			return IsPointWithinSegmentDistanceXYExact(Q, S, E, Radius);
		}
		return QueryDistance * Cross <= Radius;
	}

	/** Whether `Point` is within the planar (XY) radius of the polyline
	 *  through `Waypoints`, treating `PathOrigin` as the implicit FIRST
	 *  endpoint of the polyline.
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
	 *  not just the remaining portion. Empty waypoints returns true
	 *  (interpreted as "no path drift" — repath gate falls through). */
	bool IsPointWithinPolylineDistance(
		const FFixedVector& Point,
		const FFixedVector& PathOrigin,
		const TArray<FFixedVector>& Waypoints,
		FFixedPoint Radius)
	{
		const int32 N = Waypoints.Num();
		if (N == 0) return true;

		// Implicit prefix segment: [PathOrigin → Waypoints[0]]. Captures
		// the "approach the first waypoint along its line" semantic that
		// the smoother's CellPath[0] skip would otherwise lose.
		if (IsPointWithinSegmentDistanceXY(
			Point, PathOrigin, Waypoints[0], Radius))
		{
			return true;
		}

		// Actual polyline segments. Iterates [Waypoints[i] → Waypoints[i+1]]
		// for i in [0, N-2].
		for (int32 i = 0; i + 1 < N; ++i)
		{
			if (IsPointWithinSegmentDistanceXY(
				Point, Waypoints[i], Waypoints[i + 1], Radius))
			{
				return true;
			}
		}
		return false;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
bool UE::SeinARTSTests::IsPointWithinMoveToSegmentForTest(
	const FFixedVector& Point,
	const FFixedVector& Start,
	const FFixedVector& End,
	FFixedPoint Radius)
{
	return IsPointWithinSegmentDistanceXY(
		Point, Start, End, Radius);
}
#endif

void USeinMoveToAction::Initialize(const FFixedVector& InDestination)
{
	Destination = InDestination;
	// Resolved on first TickAction once NavComp is available.
	AcceptanceRadius = FFixedPoint::Zero;
	CurrentWaypointIndex = 0;
	bPathResolved = false;
	bAuthoritativeDestination = false;
	TimeSinceLastRepath = FFixedPoint::Zero;
	ConsecutiveRepathFailures = 0;
	BestDistToFinal = FFixedPoint::FromInt(1000);
	TimeStalledNearGoal = FFixedPoint::Zero;
	HoldTime = FFixedPoint::Zero;
	NextEscalationAt = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10); // first ladder boundary: 0.3s
	bStage1Fired = false;
	bForceRepathNow = false;
	bEscapeMode = false;
	EscapeTarget = FFixedVector::ZeroVector;
	EscapeAcceptanceRadius = FFixedPoint::Zero;
	EscapeHoldTime = FFixedPoint::Zero;
	EscapeAttempts = 0;
	TotalEscapeEntries = 0;
	FootprintRadius = FFixedPoint::Zero;
	StallBand = FFixedPoint::Zero;
	Path.Clear();
	Movement = nullptr;
	bMovementFinalized = false;
}

USeinMoveToAction::EInitialPathTickResult
USeinMoveToAction::ResolveInitialPath(
	FFixedPoint DeltaTime,
	USeinWorldSubsystem& World,
	FSeinEntity& Entity,
	FSeinMovementComponent& MovementData,
	const FSeinNavigationComponent* NavigationData,
	USeinNavigation* Navigation,
	USeinNavigationSubsystem* NavigationSubsystem)
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
		? MoveSub->GetOrCreateMovementInstance(
			OwnerEntity, MovementData)
		: nullptr;
	if (!Movement)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return EInitialPathTickResult::Terminal;
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
		Entity,
		&MovementData,
		NavigationData,
		Destination,
		Navigation,
		NavigationSubsystem,
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
		const FFixedVector StartPos = Entity.Transform.GetLocation();
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
	// preserve bPathResolved=false. Dropping Movement just clears this action's
	// BORROWED ref (the persistent instance stays in the registry).
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
		return EInitialPathTickResult::Waiting;
	}
#if !UE_BUILD_SHIPPING
	InitialThrottleStreak = 0;
#endif
	if (Result == ESeinPathResult::NoNavigation)
	{
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoNavigation));
		return EInitialPathTickResult::Terminal;
	}
	if (Result != ESeinPathResult::Found || Path.Waypoints.Num() == 0)
	{
		UE_LOG(LogSeinMove, Verbose,
			TEXT("MoveToAction initial PlanPath: failing with PathNotFound — "
			     "result=%d waypoints=%d (entity %s)"),
			static_cast<int32>(Result), Path.Waypoints.Num(),
			*OwnerEntity.ToString());
		Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
		return EInitialPathTickResult::Terminal;
	}

	// Diagnostic-only: spot the "bIsValid=true with 0 drivable segments"
	// case that produces the visible "Moving → Completed → Cancelled"
	// no-op sequence — wheeled path-replay sees 0 segments and instantly
	// classifies as "end of path." Logged here so the no-op is identified
	// in the log stream without changing behavior; the structural fix
	// (e.g., invalidate the path in FindPath) is a separate change once
	// we've confirmed the diagnosis from the logs.
	if (Movement->GetMinTurnRadius(&MovementData) > FFixedPoint::Zero
		&& Path.Segments.Num() == 0)
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
	PathOriginAgentPos = Entity.Transform.GetLocation();

	// Resolve acceptance radius from the unit's navigation component —
	// properly a per-unit characteristic (footprint / turn radius), not
	// a per-call concern. Designers tune it on the nav component.
	// Falls back to 50cm when NavigationData absent so commands still complete
	// instead of pinning at "almost arrived" forever.
	AcceptanceRadius = NavigationData
		? NavigationData->AcceptanceRadius
		: FSeinNavigationComponent::DefaultArrivalAcceptance();

	// Body radius (once per order) + the shared near-goal settle band: the
	// stall failsafe SETTLES inside it, the hold-escape ladder is EXCLUDED
	// from it — one body-aware expression so there is never a gap (a pin
	// neither owns → the old silent stand) nor an overlap (both fire).
	// max() with the footprint because a unit ordered flush against a wall
	// stops ~footprint short of its final waypoint regardless of how small
	// the authored acceptance is; +100cm absorbs C-space rounding.
	FootprintRadius = USeinMovement::ResolveCollisionRadius(
		&World, OwnerEntity, NavigationData);
	StallBand = SaturatingPositiveScale(AcceptanceRadius, 3);
	const FFixedPoint BodyBand = SaturatingPositiveAdd(
		FootprintRadius, FFixedPoint::FromInt(100));
	if (BodyBand > StallBand) { StallBand = BodyBand; }

	// Query the composed provider registry once, then carry the result on the
	// movement context so ResolveNavCollision can honor the exact destination.
	bAuthoritativeDestination = World.IsAuthoritativeDestination(
		Destination, OwnerEntity);

	FSeinMovementContext BeginCtx{
		Entity,
		&MovementData,
		NavigationData,
		Path,
		CurrentWaypointIndex,
		FFixedVector::SquareSaturated(AcceptanceRadius),
		DeltaTime,
		Navigation,
		&World,
		OwnerEntity
	};
	BeginCtx.ExactAcceptanceRadius = AcceptanceRadius;
	// Resolve the entity's collision footprint cascade
	// (Extents → NavigationData->FallbackFootprintRadius → 0) BEFORE OnMoveBegin
	// so the movement's footprint-aware ResolveNavCollision is fully wired
	// from the very first tick. Single-shot lookup; the cache is entity-stable
	// across the move action.
	Movement->CacheFootprintFromContext(BeginCtx);
	if (UWorld* UnrealWorld = World.GetWorld())
	{
		if (USeinMovementSubsystem* MovementSub =
			UnrealWorld->GetSubsystem<USeinMovementSubsystem>())
		{
			MovementSub->MarkMovementStateDirty(OwnerEntity);
		}
	}
	// The first path is committed and the unit is genuinely departing.
	// Failures before this point leave any still-occupied frozen claim live.
	World.NotifyFrozenDestinationDeparture(OwnerEntity);
	Movement->OnMoveBegin(BeginCtx);
	return EInitialPathTickResult::Ready;
}

USeinMoveToAction::ERepathTickResult USeinMoveToAction::TickRepath(
	FFixedPoint DeltaTime,
	USeinWorldSubsystem& World,
	FSeinEntity& Entity,
	FSeinMovementComponent& MovementData,
	const FSeinNavigationComponent* NavigationData,
	USeinNavigation* Navigation,
	USeinNavigationSubsystem* NavigationSubsystem)
{
	// Escape mode owns its temporary path and freezes the repath clock. Flying
	// movements continually seek the destination directly, so they clear it.
	if (bEscapeMode)
	{
		return ERepathTickResult::Skipped;
	}
	if (Movement->BypassPathfinding())
	{
		TimeSinceLastRepath = FFixedPoint::Zero;
		return ERepathTickResult::Skipped;
	}

	TimeSinceLastRepath = TimeSinceLastRepath + DeltaTime;
	if (!NavigationData)
	{
		return ERepathTickResult::Skipped;
	}

	const ESeinRepathMode RepathMode = NavigationData->RepathMode;
	const FFixedPoint RepathInterval =
		NavigationData->RepathInterval > FFixedPoint::Zero
			? NavigationData->RepathInterval
			: FFixedPoint::One / FFixedPoint::FromInt(4);
	const int32 RepathFailureLimit = NavigationData->RepathFailureLimit > 0
		? NavigationData->RepathFailureLimit
		: 3;
	const FFixedPoint OffPathThreshold =
		NavigationData->OffPathThreshold > FFixedPoint::Zero
			? NavigationData->OffPathThreshold
			: FFixedPoint::FromInt(75);

	FFixedVector AttemptOrigin = FFixedVector::ZeroVector;
	bool bAttemptRepath = false;
	switch (RepathMode)
	{
	case ESeinRepathMode::Interval:
		bAttemptRepath =
			(TimeSinceLastRepath >= RepathInterval || bForceRepathNow)
			&& Navigation
			&& NavigationSubsystem;
		break;

	case ESeinRepathMode::OffPathOnly:
	{
		const FFixedPoint MinAttemptInterval =
			FFixedPoint::One / FFixedPoint::FromInt(10);
		if (TimeSinceLastRepath < MinAttemptInterval && !bForceRepathNow)
		{
			return ERepathTickResult::Skipped;
		}
		if (!Navigation)
		{
			return ERepathTickResult::Skipped;
		}

		AttemptOrigin = Entity.Transform.GetLocation();
		if (IsPointWithinPolylineDistance(
				AttemptOrigin,
				PathOriginAgentPos,
				Path.Waypoints,
				OffPathThreshold)
			&& !bForceRepathNow)
		{
			TimeSinceLastRepath = FFixedPoint::Zero;
			return ERepathTickResult::Skipped;
		}
		bAttemptRepath = NavigationSubsystem != nullptr;
		break;
	}
	}

	if (!bAttemptRepath)
	{
		return ERepathTickResult::Skipped;
	}

	// A force request is one-shot once an actual planner attempt starts,
	// including when that attempt is throttled.
	bForceRepathNow = false;
	FSeinPlanPathContext PlanContext{
		Entity,
		&MovementData,
		NavigationData,
		Destination,
		Navigation,
		NavigationSubsystem,
		OwnerEntity,
		&World
	};
	FSeinPath NewPath;
	const ESeinPathResult RepathResult =
		Movement->PlanPath(PlanContext, NewPath);
	if (RepathResult == ESeinPathResult::Throttled)
	{
		// Throttling is not a path failure. Resetting the mode-specific clock
		// prevents an active unit from retrying every tick and starving starts.
		TimeSinceLastRepath = FFixedPoint::Zero;
		return ERepathTickResult::Continued;
	}

	const FFixedVector AgentPosition =
		RepathMode == ESeinRepathMode::OffPathOnly
			? AttemptOrigin
			: Entity.Transform.GetLocation();
	if (RepathResult == ESeinPathResult::Found
		&& NewPath.Waypoints.Num() > 0)
	{
		Path = NewPath;
		Path.FlattenToWaypoints(GArcFlattenChordError);
		CurrentWaypointIndex = 0;
		PathOriginAgentPos = AgentPosition;
		ConsecutiveRepathFailures = 0;

		if (RepathMode == ESeinRepathMode::Interval)
		{
			UE_LOG(LogSeinMove, Verbose,
				TEXT("Repath (Interval): %d new waypoints from (%.1f,%.1f)%s"),
				NewPath.Waypoints.Num(),
				AgentPosition.X.ToFloat(),
				AgentPosition.Y.ToFloat(),
				Path.bIsPartial ? TEXT(" [PARTIAL]") : TEXT(""));
		}
		else
		{
			UE_LOG(LogSeinMove, Verbose,
				TEXT("Repath (OffPathOnly): threshold=%.1fcm exceeded, %d new waypoints from (%.1f,%.1f)%s"),
				OffPathThreshold.ToFloat(),
				NewPath.Waypoints.Num(),
				AgentPosition.X.ToFloat(),
				AgentPosition.Y.ToFloat(),
				Path.bIsPartial ? TEXT(" [PARTIAL]") : TEXT(""));
		}

		NotifyPathRecomputed();
		if (Path.bIsPartial)
		{
			NotifyPartialPath();
		}
	}
	else
	{
		++ConsecutiveRepathFailures;
		if (RepathMode == ESeinRepathMode::Interval)
		{
			UE_LOG(LogSeinMove, Verbose,
				TEXT("Repath (Interval) failed: attempt %d/%d (entity %s)"),
				ConsecutiveRepathFailures,
				RepathFailureLimit,
				*OwnerEntity.ToString());
		}
		else
		{
			UE_LOG(LogSeinMove, Verbose,
				TEXT("Repath (OffPathOnly) failed after threshold exceeded: attempt %d/%d (entity %s)"),
				ConsecutiveRepathFailures,
				RepathFailureLimit,
				*OwnerEntity.ToString());
		}

		if (ConsecutiveRepathFailures >= RepathFailureLimit)
		{
			if (RepathMode == ESeinRepathMode::Interval)
			{
				UE_LOG(LogSeinMove, Warning,
					TEXT("Repath (Interval): %d consecutive failures — failing move (entity %s, dest=(%.1f,%.1f))"),
					ConsecutiveRepathFailures,
					*OwnerEntity.ToString(),
					Destination.X.ToFloat(),
					Destination.Y.ToFloat());
			}
			else
			{
				UE_LOG(LogSeinMove, Warning,
					TEXT("Repath (OffPathOnly): %d consecutive failures — failing move (entity %s, dest=(%.1f,%.1f))"),
					ConsecutiveRepathFailures,
					*OwnerEntity.ToString(),
					Destination.X.ToFloat(),
					Destination.Y.ToFloat());
			}
			Fail(static_cast<uint8>(ESeinMoveFailureReason::PathNotFound));
			return ERepathTickResult::Terminal;
		}
	}

	// Success and sub-limit failure both consumed planner budget. Delegate
	// callbacks above intentionally observe the pre-reset clock, as before.
	TimeSinceLastRepath = FFixedPoint::Zero;
	return ERepathTickResult::Continued;
}

bool USeinMoveToAction::TickEscapeLeg(
	FFixedPoint DeltaTime,
	USeinWorldSubsystem& World,
	FSeinEntity& Entity,
	FSeinMovementComponent& MovementData,
	bool bReachedEnd)
{
	const FFixedVector EscPos = Entity.Transform.GetLocation();
	bool bExitEscape = false;
	bool bAttemptFailed = false;
	const TCHAR* EscOutcome = TEXT("");

	if (bReachedEnd)
	{
		// Validate the arrival: the overshoot guard can misfire on the
		// escape ENTRY state (persisted Velocity ~0 + facing the wall
		// passes IsOvershootArrival) — a true return OUTSIDE the escape
		// ring is that misfire, not an escape.
		const bool bGenuine = FFixedVector::IsPlanarDistanceWithin(
			EscapeTarget, EscPos, EscapeAcceptanceRadius);
		bExitEscape = true;
		bAttemptFailed = !bGenuine;
		EscOutcome = bGenuine ? TEXT("done") : TEXT("overshoot");
	}
	else
	{
		// Per-attempt exhaustion: the escape leg itself is held (the floor
		// refuses even the escape direction) — abandon after 0.6s.
		FFixedVector EscVel = MovementData.Velocity;
		EscVel.Z = FFixedPoint::Zero;
		const FFixedPoint EscFloor =
			GetDefault<USeinARTSCoreSettings>()->AvoidanceMovingSpeedFloor;
		if (EscVel.SizeSquared() <= EscFloor * EscFloor)
		{
			EscapeHoldTime = EscapeHoldTime + DeltaTime;
		}
		else
		{
			EscapeHoldTime = FFixedPoint::Zero;
		}
		if (EscapeHoldTime
			>= FFixedPoint::FromInt(3) / FFixedPoint::FromInt(5)) // 0.6s
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
		NextEscalationAt =
			FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);
		Path.Clear();
		CurrentWaypointIndex = 0;
		bPathResolved = false;
		// The resume's first-resolve commits a fresh path — reset the repath
		// clock like every other path commit, or a frozen near-due timer
		// fires a second, identical request right behind it (budget waste).
		TimeSinceLastRepath = FFixedPoint::Zero;
		// The escape leg corrupted the near-goal progress high-water —
		// a resumed approach must start a fresh window.
		BestDistToFinal = FFixedPoint::FromInt(1000);
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
			UE_LOG(LogSeinMoveTrace, Verbose,
				TEXT("[ESC] t=%d h=%d:%d STRANDED"),
				World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation);
#endif
			Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
			return true;
		}
	}
	else
	{
		// AnimBPs must not blend braking anims against a wall mid-escape.
		MovementData.bArrivalImminent = false;
	}
	return false;
}

bool USeinMoveToAction::TickHoldEscapeLadder(
	FFixedPoint DeltaTime,
	USeinWorldSubsystem& World,
	FSeinEntity& Entity,
	FSeinMovementComponent& MovementData,
	const FSeinNavigationComponent* NavigationData,
	USeinNavigation* Navigation,
	bool bReachedEnd)
{
	if (bReachedEnd || Path.Waypoints.Num() == 0)
	{
		return false;
	}

	const FFixedPoint HoldFloor =
		GetDefault<USeinARTSCoreSettings>()->AvoidanceMovingSpeedFloor;
	FFixedVector HeldVel = MovementData.Velocity;
	HeldVel.Z = FFixedPoint::Zero;
	bool bHeld = FFixedVector::IsPlanarDistanceWithin(
		FFixedVector::ZeroVector, HeldVel, HoldFloor);
	// Exclude the stall failsafe's exact accrual domain — the CONJUNCTION of
	// final-leg AND the shared body-aware settle band (final-leg alone would
	// be wrong: LoS smoothing makes the final leg cover most of a route).
	if (bHeld && CurrentWaypointIndex >= Path.Waypoints.Num() - 1)
	{
		const FFixedVector HeldLocation = Entity.Transform.GetLocation();
		if (FFixedVector::IsPlanarDistanceWithin(
			HeldLocation, Path.Waypoints.Last(), StallBand))
		{
			bHeld = false; // the stall failsafe owns near-goal stops
		}
	}

	if (!bHeld)
	{
		HoldTime = FFixedPoint::Zero;
		NextEscalationAt =
			FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);
		bStage1Fired = false;
		// Genuine applied motion ends the stuck EPISODE — the consecutive
		// exhaustion counter starts fresh at the next one. (TotalEscapeEntries
		// deliberately does NOT reset — it is the per-order oscillation cap.)
		EscapeAttempts = 0;
		return false;
	}

	// NOTE: a successful repath / Path swap deliberately does NOT reset
	// this clock — repaths SUCCEED every interval in the pinned state
	// (that is the pathology); only genuine applied motion clears it.
	HoldTime = HoldTime + DeltaTime;
	if (HoldTime < NextEscalationAt)
	{
		return false;
	}

	NextEscalationAt = NextEscalationAt
		+ FFixedPoint::FromInt(3) / FFixedPoint::FromInt(10);

	// MECHANICAL-BLOCK PROBE (boundaries only, never per-tick): is
	// the direction the unit is trying to go footprint-refused? A
	// PASSABLE probe means the zero command is the mode's own policy
	// (pivot-in-place, yield) — keep accruing, re-check at the next
	// boundary, escalate nothing.
	const FFixedVector AgentPos = Entity.Transform.GetLocation();
	const FFixedPoint FootR = FootprintRadius;
	FFixedVector PlanarWaypoint = Path.Waypoints[CurrentWaypointIndex];
	PlanarWaypoint.Z = AgentPos.Z;
	const FFixedPoint ToWpLen =
		FFixedVector::DistanceSaturated(AgentPos, PlanarWaypoint);
	bool bBlocked = false;
	if (ToWpLen > FFixedPoint::Epsilon && Navigation)
	{
		const FFixedPoint ProbeDist = SaturatingPositiveAdd(
			FootR, FFixedPoint::FromInt(50));
		const FFixedVector ToWpDirection =
			FFixedVector::GetSafeNormalDifference(
				AgentPos, PlanarWaypoint);
		const FFixedVector Probe(
			AgentPos.X + ToWpDirection.X * ProbeDist,
			AgentPos.Y + ToWpDirection.Y * ProbeDist,
			AgentPos.Z);
		bBlocked = !Movement->IsFootprintPassable(Probe, Navigation);
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
		return false;
	}
	if (!bBlocked)
	{
		return false;
	}

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
	if (Navigation)
	{
		FSeinEscapeQuery EscQ;
		EscQ.From = AgentPos;
		EscQ.Requester = OwnerEntity;
		EscQ.AgentNavLayerMask = NavigationData
			? NavigationData->NavLayerMask
			: 0x01;
		EscQ.AgentFootprintRadius = FootR;
		if (NavigationData)
		{
			EscQ.BlockedTerrainTags = NavigationData->BlockedTerrainTags;
		}
		bGotTarget = Navigation->QueryEscapeTarget(EscQ, Target);
	}
	if (bGotTarget)
	{
		FFixedVector PlanarTarget = Target;
		PlanarTarget.Z = AgentPos.Z;
		const FFixedPoint EntryDist =
			FFixedVector::DistanceSaturated(AgentPos, PlanarTarget);
		// Escape acceptance = max(50, min(footprint, EntryDist/3)),
		// and the ENTRY GATE: the target must sit decisively beyond
		// the overshoot guard's 2x-acceptance vicinity, or the leg
		// instant-arrives with zero motion (the guard passes on the
		// entry state: velocity ~0, facing the wall) and the ladder
		// would cycle forever without exhausting.
		FFixedPoint AccEsc = EntryDist / FFixedPoint::FromInt(3);
		if (FootR > FFixedPoint::Zero && AccEsc > FootR) { AccEsc = FootR; }
		if (AccEsc < FFixedPoint::FromInt(50)) { AccEsc = FFixedPoint::FromInt(50); }
		const FFixedPoint MinEntry = SaturatingPositiveAdd(
			SaturatingPositiveScale(AccEsc, 2),
			MovementData.TopSpeed * DeltaTime);
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
			EscapeAcceptanceRadius = AccEsc;
			EscapeHoldTime = FFixedPoint::Zero;
			// The escape leg corrupts the near-goal progress
			// high-water; re-arm for the resumed approach.
			BestDistToFinal = FFixedPoint::FromInt(1000);
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
			UE_LOG(LogSeinMoveTrace, Verbose,
				TEXT("[ESC] t=%d h=%d:%d STRANDED"),
				World.GetCurrentTick(), OwnerEntity.Index, OwnerEntity.Generation);
#endif
			Fail(static_cast<uint8>(ESeinMoveFailureReason::Stranded));
			return true;
		}
	}
	return false;
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

	if (!bPathResolved)
	{
		switch (ResolveInitialPath(
			DeltaTime, World, *Entity, *MoveComp, NavComp, Nav, NavSub))
		{
		case EInitialPathTickResult::Ready:
			break;
		case EInitialPathTickResult::Waiting:
			return false;
		case EInitialPathTickResult::Terminal:
			return true;
		}
	}

	if (!Movement)
	{
		// Defensive: should never happen post-bPathResolved, but fail cleanly
		// rather than crash.
		Fail(static_cast<uint8>(ESeinMoveFailureReason::NoMovementComponent));
		return true;
	}

	// Repathing remains pre-movement so a committed route is consumed on this
	// tick and a terminal failure prevents any movement on the stale path.
	if (TickRepath(
			DeltaTime, World, *Entity, *MoveComp, NavComp, Nav, NavSub)
		== ERepathTickResult::Terminal)
	{
		return true;
	}

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
		FFixedVector::SquareSaturated(AcceptanceRadius),
		DeltaTime,
		Nav,
		&World,
		OwnerEntity
	};
	TickCtx.ExactAcceptanceRadius = AcceptanceRadius;
	// Escape-mode context overrides (ctx-local; the action members are preserved
	// for resume): the escape leg is never an authoritative destination (the
	// cover-slot exemption would re-target the nav floor at the escape cell),
	// and its acceptance is the entry-gated escape ring, not the order's.
	TickCtx.bAuthoritativeDestination = bEscapeMode ? false : bAuthoritativeDestination;
	if (bEscapeMode)
	{
		TickCtx.AcceptanceRadiusSq =
			FFixedVector::SquareSaturated(EscapeAcceptanceRadius);
		TickCtx.ExactAcceptanceRadius = EscapeAcceptanceRadius;
	}
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
		return TickEscapeLeg(
			DeltaTime, World, *Entity, *MoveComp, bReachedEnd);
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
		const FFixedPoint DistFinal = FFixedVector::DistanceSaturated(
			FFixedVector(AgentPos.X, AgentPos.Y, FFixedPoint::Zero),
			FFixedVector(FinalWp.X, FinalWp.Y, FFixedPoint::Zero));
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
	// see StallBand) — so only a unit essentially AT its goal (as near as its BODY allows) settles,
	// never one still approaching (that was the old crowd-aware band's "forgotten units" bug). The
	// body-aware floor keeps goal-flush wall pins OURS rather than the hold-escape ladder's — the
	// ladder escaping a unit that is simply as-close-as-it-fits would oscillate forever.
	if (!bReachedEnd && Path.Waypoints.Num() > 0
		&& CurrentWaypointIndex >= Path.Waypoints.Num() - 1)
	{
		const FFixedVector AgentPos = Entity->Transform.GetLocation();
		const FFixedPoint DistFinal = FFixedVector::DistanceSaturated(
			FFixedVector(AgentPos.X, AgentPos.Y, FFixedPoint::Zero),
			FFixedVector(
				Path.Waypoints.Last().X,
				Path.Waypoints.Last().Y,
				FFixedPoint::Zero));
		const bool bWithinStallBand = FFixedVector::IsPlanarDistanceWithin(
			AgentPos, Path.Waypoints.Last(), StallBand);

		if (!bWithinStallBand)
		{
			// Outside the tight band — re-arm the progress high-water + clock for a fresh approach.
			BestDistToFinal = DistFinal;
			TimeStalledNearGoal = FFixedPoint::Zero;
		}
		else
		{
			// Clamp the high-water up on the first in-band tick (it inits to a large sentinel).
			if (DistFinal > BestDistToFinal) BestDistToFinal = DistFinal;
			// Meaningful-closing test in ACTUAL distance (a squared additive epsilon vanishes at
			// range). 10 cm: larger than collision jitter, far smaller than a genuine approach.
			if (SaturatingPositiveAdd(
				DistFinal, FFixedPoint::FromInt(10)) < BestDistToFinal)
			{
				BestDistToFinal = DistFinal;
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
						DistFinal.ToFloat(), AcceptanceRadius.ToFloat());
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
	if (TickHoldEscapeLadder(
		DeltaTime, World, *Entity, *MoveComp, NavComp, Nav, bReachedEnd))
	{
		return true;
	}

	if (bReachedEnd)
	{
		// A partial path completes at a best-effort endpoint, not the frozen
		// destination. Only an exact-route arrival earns settled authority.
		if (!Path.bIsPartial)
		{
			World.ConfirmFrozenDestinationArrival(
				OwnerEntity, Destination);
		}
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
