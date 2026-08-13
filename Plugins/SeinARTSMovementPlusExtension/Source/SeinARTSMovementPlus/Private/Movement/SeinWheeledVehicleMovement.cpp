/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.cpp
 * @brief   Bicycle-kinematics controller with Reeds-Shepp-style start-maneuver
 *          planning (see SeinWheeledManeuver.h) and a typed-segment driver.
 */

#include "Movement/SeinWheeledVehicleMovement.h"
#include "EngineDefines.h"
#include "Movement/SeinWheeledManeuver.h"
#include "Movement/SeinPlannerHandle.h"
#include "Movement/SeinMoverHandle.h"
#include "SeinARTSMovementModule.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Lib/SeinMovementPlusBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Data/SeinWheeledMovementData.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSeinWheeled, Log, All);

namespace
{
	// Compile-time driver constants (determinism rule: never per-run tunables).
	// Speed below which the cusp direction latch may flip (world units/sec).
	const FFixedPoint CuspFlipSpeed = FFixedPoint::FromInt(30);
	// Carrot distance along a straight maneuver leg (world units).
	const FFixedPoint ManeuverLookAhead = FFixedPoint::FromInt(250);
	// Proximity at which a maneuver segment counts as consumed (world units).
	const FFixedPoint SegmentCloseRadius = FFixedPoint::FromInt(50);
	// Arc-tracking correction caps (radians) and the radial error → steer gain
	// distance (a full radial correction at this many units of offset).
	const FFixedPoint SteerHeadingCorrCap = FFixedPoint::Pi / FFixedPoint::FromInt(8);
	const FFixedPoint SteerRadialCorrCap  = FFixedPoint::Pi / FFixedPoint::FromInt(12);
	const FFixedPoint RadialGainDist      = FFixedPoint::FromInt(400);
	// Arc angular-progress epsilon (radians, ~3 deg).
	const FFixedPoint ArcSweepEps = FFixedPoint::Pi / FFixedPoint::FromInt(60);
	// Stuck detection / recovery.
	const FFixedPoint StuckSpeedMin       = FFixedPoint::FromInt(50);
	const FFixedPoint StuckTriggerSeconds = FFixedPoint::Half;
	const FFixedPoint RecoverySeconds     = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(5); // 0.6 s
	const FFixedPoint RecoverySpeedCap    = FFixedPoint::FromInt(250);
	// Maneuver leg held near zero speed this long → abandon the head into
	// carrot pursuit (traffic escape). Longer than any cusp/from-rest ramp.
	// A per-entity deterministic jitter (handle % 8 × 0.1 s) is ADDED at the
	// comparison site: two equal-weight vehicles maneuvering head-on both
	// brake-yield symmetrically, and without the jitter both would abandon on
	// the SAME deterministic tick and re-plan back into mirror-lock. (Residual
	// collision when handle indices are ≡ mod 8 — rare, and the pair still
	// resolves via the differing replans that follow.)
	const FFixedPoint ManeuverStallAbandonSeconds = FFixedPoint::FromInt(6) / FFixedPoint::FromInt(5); // 1.2 s
	// Speed below which the cusp PRE-STEER engages: while braking the last
	// stretch into a cusp, the wheels crank toward the next leg's lock (a real
	// driver turns the wheel during the stop). Gated low so the approach
	// stays straight — cranked-wheel yaw drift below this speed is negligible
	// (~0.03 rad over the brake-out).
	const FFixedPoint CuspPreSteerSpeed = CuspFlipSpeed * FFixedPoint::FromInt(3);
	// Orbit backstop: |yaw| swept without waypoint/segment progress.
	const FFixedPoint OrbitYawLimit = FFixedPoint::Pi * FFixedPoint::FromInt(5) / FFixedPoint::Two; // 2.5*pi

	FFixedPoint AbsFP2(FFixedPoint V) { return V < FFixedPoint::Zero ? -V : V; }
	FFixedPoint MinFP2(FFixedPoint A, FFixedPoint B) { return A < B ? A : B; }
	FFixedPoint MaxFP2(FFixedPoint A, FFixedPoint B) { return A > B ? A : B; }
	FFixedPoint ScalePositiveRadius(FFixedPoint Radius, FFixedPoint Scale)
	{
		if (Radius > FFixedPoint::Zero && Scale > FFixedPoint::Zero
			&& Radius > FFixedPoint::MaxValue / Scale)
		{
			return FFixedPoint::MaxValue;
		}
		return Radius * Scale;
	}

	/** Robust angular progress along an arc segment: how much of |Sweep| the
	 *  position at `Pos` has consumed, with the wrap ambiguity resolved by
	 *  splitting the leftover circle between "just past the end" and "still
	 *  before the start". */
	FFixedPoint ArcProgress(const FSeinPathSegment& S, const FFixedVector& Pos)
	{
		const FFixedPoint AbsSweep = AbsFP2(S.SweepAngle);
		if (S.Radius <= FFixedPoint::Epsilon || AbsSweep <= FFixedPoint::Epsilon) return AbsSweep;
		const FFixedPoint SweepSign = (S.SweepAngle >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
		const FFixedPoint Phi     = SeinMath::Atan2(Pos.Y - S.Center.Y, Pos.X - S.Center.X);
		const FFixedPoint PhiFrom = SeinMath::Atan2(S.From.Y - S.Center.Y, S.From.X - S.Center.X);
		const FFixedPoint Raw = SeinWheeledManeuver::WrapPositive(SweepSign * (Phi - PhiFrom));
		if (Raw <= AbsSweep) return Raw;
		const FFixedPoint TwoPi = FFixedPoint::Pi * FFixedPoint::Two;
		const FFixedPoint Mid = AbsSweep + (TwoPi - AbsSweep) * FFixedPoint::Half;
		return (Raw < Mid) ? AbsSweep : FFixedPoint::Zero;
	}

	/** Geometric completion test for a maneuver segment. Arcs complete on
	 *  ANGULAR progress only — the end-plane test false-fires at the very
	 *  START of a >=180-deg sweep, and the 50 cm proximity shortcut exceeds
	 *  the whole chord of a minimum swing leg on tight chassis (R_min < ~190),
	 *  which would chain-skip the leg unmoved. ArcProgress already reads
	 *  ~AbsSweep for any pose near the endpoint, so proximity adds nothing for
	 *  arcs. Straights complete on the end-plane crossover or proximity. */
	bool SegmentComplete(const FSeinPathSegment& S, const FFixedVector& Pos)
	{
		if (S.Type == ESeinPathSegmentType::Arc && S.Radius > FFixedPoint::Epsilon)
		{
			return ArcProgress(S, Pos) >= AbsFP2(S.SweepAngle) - ArcSweepEps;
		}
		FFixedVector ToEnd = Pos - S.To;
		ToEnd.Z = FFixedPoint::Zero;
		if (ToEnd.SizeSquared() <= SegmentCloseRadius * SegmentCloseRadius) return true;
		FFixedVector Dir = S.To - S.From;
		Dir.Z = FFixedPoint::Zero;
		if (Dir.SizeSquared() <= FFixedPoint::Epsilon) return true;
		Dir = FFixedVector::GetSafeNormal(Dir);
		return (ToEnd.X * Dir.X + ToEnd.Y * Dir.Y) >= FFixedPoint::Zero;
	}

	/** Wrap-safe "is this planar delta definitely far away" precheck: 32.32
	 *  squares wrap past ~46,340 units, so any comparison of SizeSquared/Size
	 *  against a small radius must first rule out far vectors component-wise. */
	bool IsPlanarFar(const FFixedVector& Delta)
	{
		const FFixedPoint Cap = FFixedPoint::FromInt(20000);
		return AbsFP2(Delta.X) >= Cap || AbsFP2(Delta.Y) >= Cap;
	}
}

USeinWheeledVehicleMovement::USeinWheeledVehicleMovement() = default;

UScriptStruct* USeinWheeledVehicleMovement::GetMovementDataStruct() const
{
	return FSeinWheeledMovementData::StaticStruct();
}

FFixedPoint USeinWheeledVehicleMovement::GetDeceleration(const FSeinMovementComponent* MovementData) const
{
	const FSeinWheeledMovementData* Data = MovementData ? MovementData->MovementClassData.GetPtr<FSeinWheeledMovementData>() : nullptr;
	return Data ? Data->Deceleration : FSeinWheeledMovementData().Deceleration;
}

FFixedPoint USeinWheeledVehicleMovement::GetMinTurnRadius(const FSeinMovementComponent* MovementData) const
{
	// Bicycle identity: R_min = wheelbase / tan(max steer). Guards against
	// degenerate MaxSteerAngle (<= 0) and tan() blowing up near pi/2.
	// Reads Wheelbase + MaxSteerAngle from the unwrapped wheeled sub-data;
	// falls back to struct defaults when MovementClassData is unauthored
	// (so a freshly-spawned BP that hasn't filled in MovementClassData still
	// reports a sensible MinTR to the nav layer).
	const FSeinWheeledMovementData DefaultsWheeled;
	const FSeinWheeledMovementData* WheeledPtr = MovementData
		? MovementData->MovementClassData.GetPtr<FSeinWheeledMovementData>()
		: nullptr;
	const FSeinWheeledMovementData& Wheeled = WheeledPtr ? *WheeledPtr : DefaultsWheeled;

	if (Wheeled.MaxSteerAngle <= FFixedPoint::Epsilon) return FFixedPoint::Zero;
	const FFixedPoint TanSteer = SeinMath::Tan(Wheeled.MaxSteerAngle);
	if (TanSteer <= FFixedPoint::Epsilon) return FFixedPoint::Zero;
	return Wheeled.Wheelbase / TanSteer;
}

void USeinWheeledVehicleMovement::ResetDriverState()
{
	SegCursor = 0;
	TailStartSeg = 0;
	bDriveReverseLatch = false;
	CachedPathWaypointNum = -1;
	CachedPathSegmentNum = -1;
	CachedPathFirstWp = FFixedVector::ZeroVector;
	CachedPathLastWp = FFixedVector::ZeroVector;
	CachedPathTotalCost = FFixedPoint::Zero;
	CachedPathFirstSegTo = FFixedVector::ZeroVector;
	LastEntryPos = FFixedVector::ZeroVector;
	bLastEntryPosValid = false;
	StuckTime = FFixedPoint::Zero;
	ManeuverStallTime = FFixedPoint::Zero;
	RecoveryTime = FFixedPoint::Zero;
	RecoveryDir = 0;
	YawAccumSinceProgress = FFixedPoint::Zero;
	LastProgressWaypointIndex = -1;
}

void USeinWheeledVehicleMovement::OnMoveEnd(FSeinEntity& Entity)
{
	Super::OnMoveEnd(Entity);
	// New orders must never see stale driver state at plan time: the initial
	// PlanPath of the NEXT order runs BEFORE OnMoveBegin's reset, and the
	// engage-hysteresis read (TailStartSeg/SegCursor) would otherwise leak the
	// finished order's in-maneuver flag into the fresh plan.
	CurrentSteer = FFixedPoint::Zero;
	ResetDriverState();
	bIsReversing = false;
}

void USeinWheeledVehicleMovement::UpdateSettledRenderState(
	const FSeinSettledMovementRenderContext& Context,
	const FSeinMovementComponent& MovementData,
	FSeinMovementRenderStateWriter& Writer) const
{
	using namespace UE::SeinARTSMovementPlus::Telemetry;
	const FSeinWheeledMovementData* Wheeled =
		MovementData.MovementClassData.GetPtr<FSeinWheeledMovementData>();
	if (!Wheeled || !Context.bHasPreviousSample
		|| Context.DeltaTime <= FFixedPoint::Epsilon)
	{
		Writer.Reset();
		Writer.SetValue(SteeringAngleSlot, CurrentSteer);
		return;
	}

	const FFixedPoint PreviousYaw =
		YawFromRotation(Context.PreviousTransform.Rotation);
	const FFixedPoint CurrentYaw =
		YawFromRotation(Context.CurrentTransform.Rotation);
	const FFixedPoint YawRate =
		ShortestAngleDelta(PreviousYaw, CurrentYaw) / Context.DeltaTime;
	const FFixedPoint PreviousDriverSpeed =
		Context.PreviousDriverVelocity.Size();
	const FFixedPoint DriverSpeed = Context.DriverVelocity.Size();
	const FFixedPoint SpeedDelta =
		DriverSpeed - PreviousDriverSpeed;
	const FFixedPoint AccelDenom =
		Wheeled->Acceleration * Context.DeltaTime;
	const FFixedPoint DecelDenom =
		Wheeled->Deceleration * Context.DeltaTime;
	const FFixedPoint Throttle =
		SpeedDelta > FFixedPoint::Zero
			&& AccelDenom > FFixedPoint::Epsilon
		? Clamp01(SpeedDelta / AccelDenom)
		: FFixedPoint::Zero;
	const FFixedPoint Brake =
		SpeedDelta < FFixedPoint::Zero
			&& DecelDenom > FFixedPoint::Epsilon
		? Clamp01((-SpeedDelta) / DecelDenom)
		: FFixedPoint::Zero;

	const FFixedVector Forward =
		Context.CurrentTransform.Rotation.RotateVector(
			FFixedVector::ForwardVector);
	const FFixedVector SettledDelta =
		Context.SettledVelocity * Context.DeltaTime;
	const FFixedPoint SettledForwardSpeed =
		Context.SettledVelocity.X * Forward.X
			+ Context.SettledVelocity.Y * Forward.Y;
	const FFixedPoint SettledForwardDistance =
		SettledDelta.X * Forward.X + SettledDelta.Y * Forward.Y;
	const FFixedPoint PreviousTravelDistance =
		Writer.GetValue(WheelTravelDistanceSlot);

	Writer.SetValue(SteeringAngleSlot, CurrentSteer);
	Writer.SetValue(YawRateSlot, YawRate);
	Writer.SetValue(NormalizedThrottleSlot, Throttle);
	Writer.SetValue(NormalizedBrakeSlot, Brake);
	Writer.SetValue(
		WheelTravelDistanceSlot,
		AccumulateWheelTravel(
			PreviousTravelDistance, SettledForwardDistance));
	Writer.SetValue(SettledForwardSpeedSlot, SettledForwardSpeed);
}

void USeinWheeledVehicleMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// Base dispatcher: fires BP_OnMoveBegin for BP subclasses and re-runs the
	// per-order tuning hydration (both were silently skipped before).
	Super::OnMoveBegin(Ctx);

	if (!Ctx.MovementData) return;
	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;

	// Wheels self-center per move action. Velocity intentionally preserved
	// so a vehicle reordered mid-drive doesn't instant-stop.
	CurrentSteer = FFixedPoint::Zero;
	ResetDriverState();
	bIsReversing = false;

	// LEGACY one-shot auto-reverse latch — only when maneuver planning is off
	// (the planner expresses reverse as typed segments instead, re-decided on
	// every repath rather than latched for the whole order).
	const FSeinWheeledMovementData DefaultsWheeled;
	const FSeinWheeledMovementData* WheeledPtr = MovementData.MovementClassData.GetPtr<FSeinWheeledMovementData>();
	const FSeinWheeledMovementData& Wheeled = WheeledPtr ? *WheeledPtr : DefaultsWheeled;
	if (!Wheeled.bManeuverPlanning)
	{
		const int32 N = Path.Waypoints.Num();
		bIsReversing = (N > 0) && ShouldAutoReverse(
			Entity.Transform.GetLocation(),
			Entity.Transform.Rotation,
			Path.Waypoints[N - 1],
			MovementData);
	}
}

FSeinMotion USeinWheeledVehicleMovement::ComputeArrivalMotion_Implementation(USeinMoverHandle* Mover)
{
	// Roll-through arrival: keep the residual velocity (the kinematic arrival
	// cap has already braked it toward the ring edge) and let the idle
	// coast-down finish the stop through GetDeceleration — a vehicle eases to
	// rest instead of snapping. Facing untouched.
	FSeinMotion Motion;
	const FSeinMovementContext* C = Mover ? Mover->GetContext() : nullptr;
	if (C && C->MovementData)
	{
		Motion.Velocity = C->MovementData->Velocity;
	}
	return Motion;
}

ESeinPathResult USeinWheeledVehicleMovement::PlanPath(const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const
{
	// Coarse stage: the base sealed dispatcher (budgeted A* / straight-line for
	// flyers; BP-overridable via the Plan Path event). Throttled / NotFound /
	// NoNavigation pass through untouched — the action's retry machinery
	// depends on seeing them verbatim.
	const ESeinPathResult Result = Super::PlanPath(Ctx, OutPath);
	if (Result != ESeinPathResult::Found || !Ctx.MovementData) return Result;
	if (BypassPathfinding()) return Result;
	if (OutPath.Waypoints.Num() == 0) return Result;
	// A path that ALREADY carries typed segments was authored by a BP Plan
	// Path override — it owns its geometry; never clobber it with a re-fit.
	if (OutPath.HasTypedSegments()) return Result;

	const FSeinWheeledMovementData DefaultsWheeled;
	const FSeinWheeledMovementData* WheeledPtr = Ctx.MovementData->MovementClassData.GetPtr<FSeinWheeledMovementData>();
	const FSeinWheeledMovementData& Wheeled = WheeledPtr ? *WheeledPtr : DefaultsWheeled;
	if (!Wheeled.bManeuverPlanning) return Result;

	const FFixedPoint RMin = GetMinTurnRadius(Ctx.MovementData);
	if (RMin <= FFixedPoint::Zero) return Result; // pivot-capable — no maneuver constraint

	const FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FFixedPoint RevTop = (MovementData.ReverseTopSpeed > FFixedPoint::Zero)
		? MovementData.ReverseTopSpeed : MovementData.TopSpeed * FFixedPoint::Half;

	SeinWheeledManeuver::FInputs In;
	In.Pos = Ctx.Entity.Transform.GetLocation();
	In.Yaw = YawFromRotation(Ctx.Entity.Transform.Rotation);
	In.MinTurnRadius = RMin;
	// Cruise radius capped at 100 m: a degenerate TurnRate would otherwise
	// push tangent-solve geometry past the fixed-point square wrap (~463 m).
	In.CruiseTurnRadius = (MovementData.TurnRate > FFixedPoint::Epsilon)
		? MinFP2(MaxFP2(RMin, MovementData.TopSpeed / MovementData.TurnRate), FFixedPoint::FromInt(10000))
		: RMin;
	In.FootprintRadius = ResolveCollisionRadius(Ctx.World, Ctx.SelfHandle, Ctx.NavData);
	In.Agent.Requester = Ctx.SelfHandle;
	In.Agent.AgentFootprintRadius = In.FootprintRadius;
	if (Ctx.NavData)
	{
		In.Agent.BlockedTerrainTags = Ctx.NavData->BlockedTerrainTags;
		In.Agent.AgentNavLayerMask = Ctx.NavData->NavLayerMask;
		In.Agent.AgentWallPaddingCells = Ctx.NavData->WallPadding;
	}
	In.ReverseSpeedPenalty = (RevTop > FFixedPoint::Epsilon)
		? MaxFP2(FFixedPoint::One, MovementData.TopSpeed / RevTop) : FFixedPoint::Two;
	In.ForwardPathBias = MaxFP2(FFixedPoint::One, Wheeled.ForwardPathBias);
	In.ReverseEngageDistance = MovementData.ReverseEngageDistanceThreshold;
	In.ReverseEngageDot = MovementData.ReverseEngageDotThreshold;
	In.ReversePlanMaxDistance = Wheeled.ReversePlanMaxDistance;
	// Wheeled-mode reverse gate: the sub-data default (ON) OR the unit-level
	// opt-in — wheeled vehicles reverse out of the box, untick both to forbid.
	In.bCanReverse = Wheeled.bCanReverse || MovementData.bCanReverse;
	// Replan continuity: read the current travel direction from hashed state so
	// interval repaths mid-reverse prefer to keep reversing. A recovery-nudge
	// reverse is NOT a planned reverse leg — it must not force the ladder onto
	// reverse-starting candidates.
	{
		const FFixedVector Fwd = Ctx.Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
		const FFixedPoint VelDot = MovementData.Velocity.X * Fwd.X + MovementData.Velocity.Y * Fwd.Y;
		const bool bRecoveryReverse = RecoveryTime > FFixedPoint::Zero && RecoveryDir < 0;
		In.bCurrentlyReversing = !bRecoveryReverse
			&& (VelDot < FFixedPoint::Zero)
			&& MovementData.Velocity.SizeSquared() > CuspFlipSpeed * CuspFlipSpeed;
	}
	// Engage hysteresis: while a maneuver head is being DRIVEN (instance state
	// — deterministic, derived from hashed history; reset per order via
	// OnMoveEnd), replans keep engaging down to a much lower heading error so
	// an in-progress U-turn/K-turn produces its continuation instead of being
	// truncated into braked pursuit the moment the error dips under the cold
	// threshold. Cold engage stays the toolkit default (~100 deg); continue
	// threshold 45 deg hands off below the sharp-turn-brake band (60 deg) so
	// the pursuit finish is smooth.
	if (TailStartSeg > 0 && SegCursor < TailStartSeg)
	{
		In.EngageAngle = FFixedPoint::Pi / FFixedPoint::FromInt(4);
	}
	In.Nav = Ctx.Nav;

	SeinWheeledManeuver::FPlan Plan;
	if (!SeinWheeledManeuver::PlanStartManeuver(In, OutPath.Waypoints, Plan)) return Result;

	// Emit through the planner handle (Super unbound it after its dispatch —
	// rebind with the same localized-const_cast idiom the base uses).
	const TArray<FFixedVector> Coarse = OutPath.Waypoints;
	const bool bWasPartial = OutPath.bIsPartial;
	USeinWheeledVehicleMovement* MutableThis = const_cast<USeinWheeledVehicleMovement*>(this);
	if (!MutableThis->CachedPlannerHandle)
	{
		MutableThis->CachedPlannerHandle = NewObject<USeinPlannerHandle>(MutableThis);
	}
	USeinPlannerHandle* Handle = MutableThis->CachedPlannerHandle;
	Handle->SetContext(&Ctx, &OutPath);
	Handle->ClearPath();
	for (const SeinWheeledManeuver::FLeg& Leg : Plan.Legs)
	{
		if (Leg.bArc)
		{
			Handle->AddArcSegment(Leg.From, Leg.To, Leg.Center, Leg.Radius, Leg.Sweep, Leg.bReverse);
		}
		else
		{
			Handle->AddStraightSegment(Leg.From, Leg.To, Leg.bReverse);
		}
	}
	if (Plan.JoinWaypointIndex >= 0)
	{
		// The last maneuver leg ends exactly AT Coarse[JoinWaypointIndex]; the
		// tail continues the coarse polyline so the terminal waypoint stays the
		// exact ordered destination (preview invariant).
		for (int32 i = Plan.JoinWaypointIndex; i + 1 < Coarse.Num(); ++i)
		{
			Handle->AddStraightSegment(Coarse[i], Coarse[i + 1], false);
		}
	}
	Handle->FinalizeTypedPath(bWasPartial);
	Handle->SetContext(nullptr, nullptr);
	return ESeinPathResult::Found;
}

bool USeinWheeledVehicleMovement::RefreshPathCache(const FSeinPath& Path, FFixedPoint CurrentSpeed, FFixedPoint CuspFlipSpd)
{
	const int32 WpNum = Path.Waypoints.Num();
	const int32 SegNum = Path.Segments.Num();
	const FFixedVector First = WpNum > 0 ? Path.Waypoints[0] : FFixedVector::ZeroVector;
	const FFixedVector Last = WpNum > 0 ? Path.Waypoints[WpNum - 1] : FFixedVector::ZeroVector;
	const FFixedVector FirstSegTo = SegNum > 0 ? Path.Segments[0].To : FFixedVector::ZeroVector;
	const bool bSame =
		WpNum == CachedPathWaypointNum && SegNum == CachedPathSegmentNum
		&& First.X == CachedPathFirstWp.X && First.Y == CachedPathFirstWp.Y && First.Z == CachedPathFirstWp.Z
		&& Last.X == CachedPathLastWp.X && Last.Y == CachedPathLastWp.Y && Last.Z == CachedPathLastWp.Z
		&& Path.TotalCost == CachedPathTotalCost
		&& FirstSegTo.X == CachedPathFirstSegTo.X && FirstSegTo.Y == CachedPathFirstSegTo.Y;
	if (bSame) return false;

	CachedPathWaypointNum = WpNum;
	CachedPathSegmentNum = SegNum;
	CachedPathFirstWp = First;
	CachedPathLastWp = Last;
	CachedPathTotalCost = Path.TotalCost;
	CachedPathFirstSegTo = FirstSegTo;

	// Tail = everything after the last non-(forward straight) segment.
	TailStartSeg = 0;
	for (int32 i = 0; i < SegNum; ++i)
	{
		const FSeinPathSegment& S = Path.Segments[i];
		if (S.Type != ESeinPathSegmentType::Straight || S.bReverse) TailStartSeg = i + 1;
	}
	SegCursor = 0;
	// Initial drive latch: keep the live travel direction while moving (the
	// cusp gate brakes before any flip); adopt the first segment's direction
	// when effectively stopped.
	const bool bFirstReverse = (TailStartSeg > 0 && SegNum > 0) ? Path.Segments[0].bReverse : false;
	bDriveReverseLatch = (AbsFP2(CurrentSpeed) > CuspFlipSpd) ? (CurrentSpeed < FFixedPoint::Zero) : bFirstReverse;
	YawAccumSinceProgress = FFixedPoint::Zero;
	LastProgressWaypointIndex = -1;
	return true;
}

bool USeinWheeledVehicleMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadius = Ctx.GetAcceptanceRadius();
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	// Unwrap wheeled-specific sub-data once. Defaults from the struct's
	// in-class initializers stand in when MovementClassData is unauthored.
	const FSeinWheeledMovementData DefaultsWheeled;
	const FSeinWheeledMovementData* WheeledPtr = MovementData.MovementClassData.GetPtr<FSeinWheeledMovementData>();
	const FSeinWheeledMovementData& Wheeled = WheeledPtr ? *WheeledPtr : DefaultsWheeled;

	const int32 N = Path.Waypoints.Num();
	if (N == 0) return true;

	// -------------------------------------------------------------------
	// 1. Recover signed scalar speed from persisted velocity vector.
	//    For non-strafing wheeled the invariant is Velocity = Forward * Speed
	//    at end of each tick; |Velocity| is the magnitude, sign of
	//    (Velocity dot Forward) gives forward/reverse.
	// -------------------------------------------------------------------
	const FFixedQuaternion EntryRot = Entity.Transform.Rotation;
	const FFixedVector EntryForward = EntryRot.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint EntryDot = MovementData.Velocity.X * EntryForward.X + MovementData.Velocity.Y * EntryForward.Y;
	const FFixedPoint EntryMag = MovementData.Velocity.Size();
	FFixedPoint CurrentSpeed = (EntryDot >= FFixedPoint::Zero) ? EntryMag : -EntryMag;
	const FFixedPoint AbsCurrentSpeed = AbsFP2(CurrentSpeed);

	const FFixedVector AgentPos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];
	const FFixedPoint CurrentYaw = YawFromRotation(EntryRot);

	// -------------------------------------------------------------------
	// 2. Path identity + segment-driver cache (rebuilds on initial plan and
	//    every repath), then geometric segment-cursor advance.
	// -------------------------------------------------------------------
	RefreshPathCache(Path, CurrentSpeed, CuspFlipSpeed);
	const int32 SegNum = Path.Segments.Num();
	const bool bHasManeuverHead = TailStartSeg > 0 && SegNum > 0;
	while (SegCursor < TailStartSeg && SegCursor < SegNum
		&& SegmentComplete(Path.Segments[SegCursor], AgentPos))
	{
		++SegCursor;
		YawAccumSinceProgress = FFixedPoint::Zero;
	}
	const bool bManeuverMode = bHasManeuverHead && SegCursor < TailStartSeg && SegCursor < SegNum;

	// Drive direction this tick. Maneuver mode owns it via the cusp latch; the
	// tail is forward by construction; the legacy path uses the one-shot latch.
	bool bDriveReverse;
	if (bManeuverMode)         { bDriveReverse = bDriveReverseLatch; }
	else if (bHasManeuverHead) { bDriveReverse = false; bDriveReverseLatch = false; }
	else                       { bDriveReverse = bIsReversing; }

	// -------------------------------------------------------------------
	// 3. Arrival check (ring / overshoot). Overshoot uses the TRAVEL heading
	//    (facing flipped when reversing) so a reverse approach doesn't misread
	//    as "heading away" and complete early. Arrival routes through
	//    DispatchArrivalMotion so the mode's arrival policy (roll-through)
	//    applies — the Tier-2 contract.
	// -------------------------------------------------------------------
	{
		const bool bWithinAcceptance =
			Ctx.IsWithinPlanarAcceptance(AgentPos, FinalWp);
		const FFixedPoint VicinityRadius = ScalePositiveRadius(
			AcceptanceRadius, FFixedPoint::Two);
		const FFixedPoint OvershootSpeedCap =
			MovementData.TopSpeed / FFixedPoint::FromInt(3);
		const FFixedQuaternion TravelRot = bDriveReverse
			? YawOnly(CurrentYaw + FFixedPoint::Pi) : EntryRot;
		// The overshoot guard exists to stop a chassis orbiting a goal it
		// can't circle into — but a maneuver head IS a planned circle-back;
		// while one is being driven, only the acceptance ring may complete.
		const bool bOvershoot = !bManeuverMode
			&& (Ctx.HasExactAcceptanceRadius()
				? IsOvershootArrivalRadius(
					AgentPos, FinalWp, TravelRot,
					CurrentSpeed, VicinityRadius, OvershootSpeedCap)
				: IsOvershootArrival(
					AgentPos, FinalWp, TravelRot,
					CurrentSpeed,
					ScalePositiveRadius(
						Ctx.AcceptanceRadiusSq, FFixedPoint::FromInt(4)),
					OvershootSpeedCap));
		if (bWithinAcceptance || bOvershoot)
		{
			DispatchArrivalMotion(Ctx);
			CurrentSteer = FFixedPoint::Zero;
			return true;
		}
	}

	// -------------------------------------------------------------------
	// 4. Cross-over waypoint advance -- harness bookkeeping (the action's
	//    notifications and stall band read the index) even while the steering
	//    itself is segment-driven.
	// -------------------------------------------------------------------
	{
		const FFixedPoint OneStep = MovementData.TopSpeed * DeltaTime;
		const FFixedPoint CloseRadius = (OneStep * FFixedPoint::Two > FFixedPoint::FromInt(50))
			? OneStep * FFixedPoint::Two : FFixedPoint::FromInt(50);
		AdvanceWaypointAlongPath(CurrentWaypointIndex, Path, AgentPos, CloseRadius);
	}
	if (CurrentWaypointIndex != LastProgressWaypointIndex)
	{
		LastProgressWaypointIndex = CurrentWaypointIndex;
		YawAccumSinceProgress = FFixedPoint::Zero;
	}

	const FFixedPoint Cruise = EffectiveTopSpeed(Ctx);
	const FFixedPoint RevTop = (MovementData.ReverseTopSpeed > FFixedPoint::Zero)
		? MovementData.ReverseTopSpeed : MovementData.TopSpeed * FFixedPoint::Half;
	const FFixedPoint SpeedYield = GetAvoidanceSpeedScale(Ctx);
	const bool bRecovering = RecoveryTime > FFixedPoint::Zero && RecoveryDir != 0;

	// Final-approach kinematic cap (v^2 = 2*a*d toward the acceptance-ring
	// EDGE) + optional linear slowdown floor — shared by every steering mode.
	// DistFinal saturates only beyond the representable scalar range, where no
	// braking or near-goal policy should apply.
	FFixedPoint MaxArrivalSpeed = FFixedPoint::FromInt(1000000);
	FFixedPoint DistFinal;
	{
		FFixedVector PlanarFinal = FinalWp;
		PlanarFinal.Z = AgentPos.Z;
		DistFinal = FFixedVector::DistanceSaturated(
			AgentPos, PlanarFinal);
		const FFixedPoint Acceptance = Ctx.GetAcceptanceRadius();
		const FFixedPoint BrakeDist = (DistFinal > Acceptance)
			? (DistFinal - Acceptance)
			: FFixedPoint::Zero;
		MaxArrivalSpeed = KinematicArrivalSpeedCap(
			BrakeDist, Wheeled.Deceleration);
		if (Wheeled.ArrivalSlowdownDistance > FFixedPoint::Zero
			&& DistFinal < Wheeled.ArrivalSlowdownDistance)
		{
			const FFixedPoint LinearCap = MovementData.TopSpeed
				* (DistFinal / Wheeled.ArrivalSlowdownDistance);
			if (LinearCap < MaxArrivalSpeed) MaxArrivalSpeed = LinearCap;
		}
	}

	// -------------------------------------------------------------------
	// 4.5 Stuck detection: sustained commanded-but-not-moving, measured
	//     ENTRY-to-ENTRY across ticks so the PostTick collision resolver's
	//     pushes are included — entity/crowd pins are caught, not only the
	//     nav-floor wall pins an in-tick before/after compare would see.
	//     Gated on bManeuverPlanning (the OFF setting stays a faithful legacy
	//     A/B control) and suppressed near the goal, where the action's
	//     crowd-stall settle owns the endgame.
	// -------------------------------------------------------------------
	const FFixedPoint NearGoalSuppress = CachedCollisionRadius + FFixedPoint::FromInt(150);
	const bool bRecoveryAllowed = Wheeled.bManeuverPlanning && DistFinal > NearGoalSuppress;
	if (bRecoveryAllowed && !bRecovering && bLastEntryPosValid)
	{
		FFixedVector EntryMoved = AgentPos - LastEntryPos;
		EntryMoved.Z = FFixedPoint::Zero;
		// Entry speed IS last tick's commanded speed (persisted velocity).
		const FFixedPoint Expected = AbsCurrentSpeed * DeltaTime;
		if (AbsCurrentSpeed > StuckSpeedMin && Expected > FFixedPoint::One
			&& !IsPlanarFar(EntryMoved)
			&& EntryMoved.Size() < Expected * FFixedPoint::Half * FFixedPoint::Half)
		{
			StuckTime += DeltaTime;
			if (StuckTime >= StuckTriggerSeconds)
			{
				const bool bEffectiveCanReverse = Wheeled.bCanReverse || MovementData.bCanReverse;
				const FFixedPoint ProbeDist = CachedCollisionRadius + FFixedPoint::FromInt(80);
				const FFixedVector Behind(AgentPos.X - EntryForward.X * ProbeDist, AgentPos.Y - EntryForward.Y * ProbeDist, AgentPos.Z);
				const FFixedVector Ahead(AgentPos.X + EntryForward.X * ProbeDist, AgentPos.Y + EntryForward.Y * ProbeDist, AgentPos.Z);
				if (bEffectiveCanReverse && IsFootprintPassable(Behind, Nav)) { RecoveryDir = -1; RecoveryTime = RecoverySeconds; }
				else if (IsFootprintPassable(Ahead, Nav))                     { RecoveryDir = +1; RecoveryTime = RecoverySeconds; }
				StuckTime = FFixedPoint::Zero;
			}
		}
		else
		{
			StuckTime = FFixedPoint::Zero;
		}
	}
	LastEntryPos = AgentPos;
	bLastEntryPosValid = true;

	// -------------------------------------------------------------------
	// 5. Steering + target speed, by mode.
	// -------------------------------------------------------------------
	FFixedPoint DesiredSteer = FFixedPoint::Zero;
	FFixedPoint TargetSpeed = FFixedPoint::Zero;
	// Legacy-mode assist inputs (stay zero in maneuver/recovery modes, which
	// disables the helping hand — planned maneuvers are honest bicycle only).
	FFixedPoint AbsYawErr = FFixedPoint::Zero;
	FFixedPoint YawErrSigned = FFixedPoint::Zero;
	bool bAllowAssist = false;
	// Set by the legacy branch, which smooths the steer itself BEFORE its
	// throttle scales read it (the original ordering); the shared smoothing
	// step below then skips.
	bool bSteerSmoothedInBranch = false;
#if UE_ENABLE_DEBUG_DRAWING
	FFixedVector DebugSteerTarget = AgentPos;
	bool bDebugSteerTargetValid = false;
#endif

	if (bRecovering)
	{
		// Probe-gated straight nudge to break a wall/crowd pin; the normal
		// repath then replans the maneuver from the freed pose. RecoveryTime
		// starts counting only after signed motion reaches the chosen direction,
		// not during the braking interval needed to shed opposite speed;
		// otherwise a fast pinned chassis can consume the whole nudge without
		// moving away.
		const int32 ActiveRecoveryDir = RecoveryDir;
		const bool bTravellingRecoveryDirection =
			(ActiveRecoveryDir < 0 && CurrentSpeed < FFixedPoint::Zero)
			|| (ActiveRecoveryDir > 0 && CurrentSpeed > FFixedPoint::Zero);
		if (bTravellingRecoveryDirection)
		{
			RecoveryTime -= DeltaTime;
		}
		const FFixedPoint Mag = MinFP2(
			RecoverySpeedCap,
			(ActiveRecoveryDir < 0) ? RevTop : Cruise);
		TargetSpeed = (ActiveRecoveryDir < 0) ? -Mag : Mag;
		bDriveReverse = ActiveRecoveryDir < 0;
		if (RecoveryTime <= FFixedPoint::Zero)
		{
			RecoveryTime = FFixedPoint::Zero;
			RecoveryDir = 0;
		}
	}
	else if (bManeuverMode)
	{
		const FSeinPathSegment& S = Path.Segments[SegCursor];

		// Cusp gate: a direction flip engages only once the chassis has braked
		// under the cusp epsilon. During the last stretch of the brake the
		// wheels PRE-STEER toward the next leg's lock (a real driver cranks
		// the wheel during the stop), so the flipped leg departs at lock
		// instead of spending ~1/SteerResponse drifting wide from center.
		if (S.bReverse != bDriveReverseLatch)
		{
			if (AbsCurrentSpeed > CuspFlipSpeed)
			{
				DesiredSteer = FFixedPoint::Zero;
				if (AbsCurrentSpeed < CuspPreSteerSpeed
					&& S.Type == ESeinPathSegmentType::Arc && S.Radius > FFixedPoint::Epsilon)
				{
					// The next leg's feed-forward lock (same formula the arc
					// tracker uses below).
					const FFixedPoint SweepSign = (S.SweepAngle >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
					const FFixedPoint DirSign = S.bReverse ? -SweepSign : SweepSign;
					DesiredSteer = ClampFP(DirSign * SeinMath::Atan(Wheeled.Wheelbase / S.Radius),
						-Wheeled.MaxSteerAngle, Wheeled.MaxSteerAngle);
				}
				TargetSpeed = FFixedPoint::Zero; // brake to the cusp
			}
			else
			{
				// Flip the latch and KEEP the pre-cranked steer — re-centering
				// here would throw away the wheel angle just set up.
				bDriveReverseLatch = S.bReverse;
			}
			bDriveReverse = bDriveReverseLatch;
		}

		if (S.bReverse == bDriveReverseLatch)
		{
			FFixedPoint SegSpeedMag;
			FFixedPoint DistToSegEnd;
			if (S.Type == ESeinPathSegmentType::Arc && S.Radius > FFixedPoint::Epsilon)
			{
				// Curvature feed-forward + heading/cross-track correction.
				const FFixedPoint SweepSign = (S.SweepAngle >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
				FFixedVector FromC = AgentPos - S.Center;
				FromC.Z = FFixedPoint::Zero;
				const FFixedPoint Phi = SeinMath::Atan2(FromC.Y, FromC.X);
				const FFixedPoint TangentYaw = Phi + SweepSign * FFixedPoint::Pi * FFixedPoint::Half;
				const FFixedPoint DesiredYaw = S.bReverse ? TangentYaw + FFixedPoint::Pi : TangentYaw;
				const FFixedPoint YawErr = ShortestAngleDelta(CurrentYaw, DesiredYaw);
				const FFixedPoint RadialErr = FromC.Size() - S.Radius; // >0 = outside the arc
				const FFixedPoint DirSign = S.bReverse ? -SweepSign : SweepSign;
				const FFixedPoint FeedForward = DirSign * SeinMath::Atan(Wheeled.Wheelbase / S.Radius);
				const FFixedPoint HeadTerm = (S.bReverse ? -FFixedPoint::One : FFixedPoint::One)
					* ClampFP(YawErr, -SteerHeadingCorrCap, SteerHeadingCorrCap);
				const FFixedPoint RadTerm = DirSign
					* ClampFP(RadialErr / RadialGainDist, -SteerRadialCorrCap, SteerRadialCorrCap);
				DesiredSteer = ClampFP(FeedForward + HeadTerm + RadTerm,
					-Wheeled.MaxSteerAngle, Wheeled.MaxSteerAngle);

				// Arc speed law: v <= TurnRate * R keeps the arc trackable under
				// the yaw-rate clamp — wide (cruise-radius) arcs run full speed,
				// tight (min-radius) arcs brake. This is the planned arc-braking.
				SegSpeedMag = S.bReverse ? RevTop : Cruise;
				if (MovementData.TurnRate > FFixedPoint::Epsilon)
				{
					SegSpeedMag = MinFP2(SegSpeedMag, MovementData.TurnRate * S.Radius);
				}
				DistToSegEnd = (AbsFP2(S.SweepAngle) - ArcProgress(S, AgentPos)) * S.Radius;
#if UE_ENABLE_DEBUG_DRAWING
				DebugSteerTarget = S.To;
				bDebugSteerTargetValid = true;
#endif
			}
			else
			{
				// Straight leg: pursue a carrot clamped to the segment line
				// (reverse legs pursue it nose-away — reverse pure pursuit).
				FFixedVector Dir = S.To - S.From;
				Dir.Z = FFixedPoint::Zero;
				const FFixedPoint Len = Dir.Size();
				FFixedVector Carrot = S.To;
				if (Len > FFixedPoint::Epsilon)
				{
					Dir = FFixedVector::GetSafeNormal(Dir);
					FFixedVector FromStart = AgentPos - S.From;
					FromStart.Z = FFixedPoint::Zero;
					const FFixedPoint T = ClampFP(FromStart.X * Dir.X + FromStart.Y * Dir.Y, FFixedPoint::Zero, Len);
					const FFixedPoint CarrotT = MinFP2(T + ManeuverLookAhead, Len);
					Carrot = FFixedVector(S.From.X + Dir.X * CarrotT, S.From.Y + Dir.Y * CarrotT, S.From.Z);
					DistToSegEnd = Len - T;
				}
				else
				{
					DistToSegEnd = FFixedPoint::Zero;
				}
				FFixedVector ToTarget = Carrot - AgentPos;
				ToTarget.Z = FFixedPoint::Zero;
				if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
				{
					const FFixedPoint DesiredYaw = S.bReverse
						? SeinMath::Atan2(-ToTarget.Y, -ToTarget.X)
						: SeinMath::Atan2(ToTarget.Y, ToTarget.X);
					const FFixedPoint YawErr = ShortestAngleDelta(CurrentYaw, DesiredYaw);
					FFixedPoint Steer = ClampFP(YawErr, -Wheeled.MaxSteerAngle, Wheeled.MaxSteerAngle);
					if (S.bReverse) Steer = -Steer;
					DesiredSteer = Steer;
				}
				SegSpeedMag = S.bReverse ? RevTop : Cruise;
#if UE_ENABLE_DEBUG_DRAWING
				DebugSteerTarget = Carrot;
				bDebugSteerTargetValid = true;
#endif
			}

			// Anticipatory braking into the next segment's entry speed: zero at
			// a cusp, the arc speed law at an arc — v = sqrt(vNext^2 + 2*a*d).
			if (SegCursor + 1 < TailStartSeg && SegCursor + 1 < SegNum)
			{
				const FSeinPathSegment& Next = Path.Segments[SegCursor + 1];
				FFixedPoint NextEntry;
				if (Next.bReverse != S.bReverse)
				{
					NextEntry = FFixedPoint::Zero;
				}
				else if (Next.Type == ESeinPathSegmentType::Arc && Next.Radius > FFixedPoint::Epsilon
					&& MovementData.TurnRate > FFixedPoint::Epsilon)
				{
					NextEntry = MinFP2(Next.bReverse ? RevTop : Cruise, MovementData.TurnRate * Next.Radius);
				}
				else
				{
					NextEntry = Next.bReverse ? RevTop : Cruise;
				}
				if (NextEntry < SegSpeedMag && Wheeled.Deceleration > FFixedPoint::Zero)
				{
					const FFixedPoint CapSq = NextEntry * NextEntry
						+ FFixedPoint::Two * Wheeled.Deceleration * MaxFP2(DistToSegEnd, FFixedPoint::Zero);
					SegSpeedMag = MinFP2(SegSpeedMag, SeinMath::Sqrt(CapSq));
				}
			}

			if (MaxArrivalSpeed < SegSpeedMag) SegSpeedMag = MaxArrivalSpeed;
			// Maneuver legs yield to avoidance by BRAKING only — bending a
			// planned arc/reverse leg would break its geometry; the collision
			// floor + resolver remain the hard guarantees.
			SegSpeedMag = SegSpeedMag * SpeedYield;
			TargetSpeed = S.bReverse ? -SegSpeedMag : SegSpeedMag;
			bDriveReverse = S.bReverse;
		}

		// Maneuver traffic-stall escape: a planned leg held near zero speed
		// (avoidance yield against a parked neighbour, or anything else the
		// stuck detector's commanded-speed gate can't see) for a sustained
		// window abandons the head and falls back to carrot pursuit, whose
		// avoidance steer-bend routes around traffic naturally. Cusp braking
		// is intentional slowness and is excluded.
		if (S.bReverse == bDriveReverseLatch && AbsCurrentSpeed < StuckSpeedMin)
		{
			ManeuverStallTime += DeltaTime;
			// Per-entity deterministic jitter breaks abandon-tick symmetry
			// between mutually-yielding vehicles (see the constant's comment).
			const FFixedPoint StallJitter =
				FFixedPoint::FromInt(Ctx.SelfHandle.Index % 8) / FFixedPoint::FromInt(10);
			if (ManeuverStallTime >= ManeuverStallAbandonSeconds + StallJitter)
			{
				SegCursor = TailStartSeg;
				ManeuverStallTime = FFixedPoint::Zero;
			}
		}
		else
		{
			ManeuverStallTime = FFixedPoint::Zero;
		}
	}
	else
	{
		// ---------------------------------------------------------------
		// LEGACY / TAIL: speed-adaptive pure-pursuit carrot on the waypoint
		// backbone — the pre-maneuver steering, kept intact for the all-
		// forward tail and for bManeuverPlanning-off units. Additions over
		// the historic code: the avoidance SpeedScale yield is consumed.
		// ---------------------------------------------------------------
		bAllowAssist = true;

		const FFixedPoint LookAheadFloor = (Wheeled.LookAheadDistance > FFixedPoint::Zero)
			? Wheeled.LookAheadDistance : FFixedPoint::FromInt(100);
		const FFixedPoint LookAhead = ComputeAdaptiveLookAhead(
			LookAheadFloor, Wheeled.LookAheadTimeHorizon, AbsCurrentSpeed);
		FFixedVector LookAheadPoint = ResolveLookAheadPoint(
			AgentPos, Path, CurrentWaypointIndex, LookAhead);
		LookAheadPoint.Z = AgentPos.Z;
		const bool bHasTarget = !FFixedVector::IsPlanarDistanceWithin(
			AgentPos, LookAheadPoint, FFixedPoint::Epsilon);
		FFixedVector ToTarget = bHasTarget
			? FFixedVector::GetSafeNormalDifference(AgentPos, LookAheadPoint)
			: FFixedVector::ZeroVector;

		// Local avoidance — bend the carrot direction in the FORWARD frame,
		// BEFORE the auto-reverse yaw-flip, so the dodge isn't inverted when
		// backing up. Soft layer; the penetration floor still guarantees no
		// overlap.
		if (bHasTarget)
		{
			ToTarget = ApplyAvoidanceSteer(Ctx, ToTarget);
		}
#if UE_ENABLE_DEBUG_DRAWING
		DebugSteerTarget = LookAheadPoint;
		bDebugSteerTargetValid = true;
#endif

		// Pure path-pull steer -- yaw error toward the carrot. bDriveReverse
		// flips desired yaw to "back faces goal" and inverts the steer for
		// bicycle reverse kinematics.
		if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
		{
			const FFixedPoint DesiredYaw = bDriveReverse
				? SeinMath::Atan2(-ToTarget.Y, -ToTarget.X)
				: SeinMath::Atan2(ToTarget.Y, ToTarget.X);
			const FFixedPoint YawErr = ShortestAngleDelta(CurrentYaw, DesiredYaw);
			YawErrSigned = YawErr;
			AbsYawErr = AbsFP2(YawErr);
			FFixedPoint PathPullSteer = ClampFP(YawErr, -Wheeled.MaxSteerAngle, Wheeled.MaxSteerAngle);
			if (bDriveReverse) PathPullSteer = -PathPullSteer;
			DesiredSteer = PathPullSteer;
		}

		// PARITY: smooth CurrentSteer toward DesiredSteer BEFORE the throttle
		// scales read it — the original tick order (smooth, then TurnScale on
		// the same-tick smoothed steer). The shared smoothing step later skips.
		{
			FFixedPoint Alpha = Wheeled.SteerResponse * DeltaTime;
			if (Alpha < FFixedPoint::Zero) Alpha = FFixedPoint::Zero;
			if (Alpha > FFixedPoint::One)  Alpha = FFixedPoint::One;
			CurrentSteer = CurrentSteer + (DesiredSteer - CurrentSteer) * Alpha;
		}
		bSteerSmoothedInBranch = true;

		// Throttle scaling -- quadratic falloff with |steer| / MaxSteer.
		FFixedPoint TurnScale = FFixedPoint::One;
		if (Wheeled.TurnSpeedFloor < FFixedPoint::One && Wheeled.MaxSteerAngle > FFixedPoint::Epsilon)
		{
			const FFixedPoint AbsSteer = AbsFP2(CurrentSteer);
			FFixedPoint T = AbsSteer / Wheeled.MaxSteerAngle;
			if (T > FFixedPoint::One) T = FFixedPoint::One;
			const FFixedPoint TSq = T * T;
			TurnScale = FFixedPoint::One - (FFixedPoint::One - Wheeled.TurnSpeedFloor) * TSq;
		}

		// Sharp-turn brake — reacts to the COMMANDED turn (raw |YawErr|)
		// immediately, while TurnSpeedFloor lags on the smoothed steer.
		FFixedPoint SharpTurnScale = FFixedPoint::One;
		if (Wheeled.SharpTurnBrakeStrength > FFixedPoint::Zero
			&& Wheeled.SharpTurnBrakeAngle < FFixedPoint::Pi
			&& AbsYawErr > Wheeled.SharpTurnBrakeAngle)
		{
			const FFixedPoint SharpRange = FFixedPoint::Pi - Wheeled.SharpTurnBrakeAngle;
			if (SharpRange > FFixedPoint::Epsilon)
			{
				FFixedPoint AngleT = (AbsYawErr - Wheeled.SharpTurnBrakeAngle) / SharpRange;
				if (AngleT > FFixedPoint::One) AngleT = FFixedPoint::One;
				FFixedPoint SpeedT = FFixedPoint::One;
				if (MovementData.TopSpeed > FFixedPoint::Epsilon)
				{
					SpeedT = AbsCurrentSpeed / MovementData.TopSpeed;
					if (SpeedT > FFixedPoint::One)  SpeedT = FFixedPoint::One;
					if (SpeedT < FFixedPoint::Zero) SpeedT = FFixedPoint::Zero;
				}
				const FFixedPoint Reduction = Wheeled.SharpTurnBrakeStrength * AngleT * SpeedT;
				SharpTurnScale = FFixedPoint::One - Reduction;
				if (SharpTurnScale < FFixedPoint::Zero) SharpTurnScale = FFixedPoint::Zero;
			}
		}

		// Low-speed reorient hold (helping-hand companion): pivot-then-go for
		// from-rest u-turns when nearly stopped AND badly misaligned.
		FFixedPoint ReorientScale = FFixedPoint::One;
		if (Wheeled.LowSpeedTurnRate > FFixedPoint::Zero
			&& Wheeled.TurnAssistFadeSpeed > FFixedPoint::Epsilon)
		{
			const FFixedPoint HoldSpeed = Wheeled.TurnAssistFadeSpeed * FFixedPoint::Half;
			const FFixedPoint HalfPi    = FFixedPoint::Pi * FFixedPoint::Half;
			if (AbsCurrentSpeed < HoldSpeed && AbsYawErr > HalfPi && HoldSpeed > FFixedPoint::Epsilon)
			{
				FFixedPoint MisalignT = (AbsYawErr - HalfPi) / HalfPi;
				if (MisalignT > FFixedPoint::One) MisalignT = FFixedPoint::One;
				FFixedPoint StoppedT = FFixedPoint::One - (AbsCurrentSpeed / HoldSpeed);
				if (StoppedT < FFixedPoint::Zero) StoppedT = FFixedPoint::Zero;
				if (StoppedT > FFixedPoint::One)  StoppedT = FFixedPoint::One;
				ReorientScale = FFixedPoint::One - MisalignT * StoppedT;
				if (ReorientScale < FFixedPoint::Zero) ReorientScale = FFixedPoint::Zero;
			}
		}

		FFixedPoint TargetSpeedMag = bDriveReverse ? RevTop : Cruise;
		TargetSpeedMag = TargetSpeedMag * TurnScale * SharpTurnScale * ReorientScale;
		// Avoidance speed-yield — give way by braking, not only turning.
		TargetSpeedMag = TargetSpeedMag * SpeedYield;
		if (MaxArrivalSpeed < TargetSpeedMag) TargetSpeedMag = MaxArrivalSpeed;
		TargetSpeed = bDriveReverse ? -TargetSpeedMag : TargetSpeedMag;
	}

#if UE_ENABLE_DEBUG_DRAWING
	// Steering-target viz. Gated on Sein.Nav.Show.SteeringVectors. Green dot =
	// the active steering target (carrot / arc endpoint), green line = agent → target.
	if (bDebugSteerTargetValid)
	{
		if (UWorld* DebugWorld = Ctx.World ? Ctx.World->GetWorld() : nullptr)
		{
			if (UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld(DebugWorld))
			{
				const float DrawLifetime = static_cast<float>(Ctx.DeltaTime.ToFloat()) + 0.01f;
				const FVector Origin(AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(), AgentPos.Z.ToFloat() + 50.0f);
				const FVector TargetPos(DebugSteerTarget.X.ToFloat(), DebugSteerTarget.Y.ToFloat(), DebugSteerTarget.Z.ToFloat() + 50.0f);
				DrawDebugPoint(DebugWorld, TargetPos, 8.0f, FColor::Green, false, DrawLifetime);
				DrawDebugLine(DebugWorld, Origin, TargetPos, FColor::Green, false, DrawLifetime, 0, 2.0f);
			}
		}
	}
#endif

	// -------------------------------------------------------------------
	// 6. Smooth CurrentSteer toward DesiredSteer (exponential approach) —
	//    unless the legacy branch already did (parity ordering).
	// -------------------------------------------------------------------
	if (!bSteerSmoothedInBranch)
	{
		FFixedPoint Alpha = Wheeled.SteerResponse * DeltaTime;
		if (Alpha < FFixedPoint::Zero) Alpha = FFixedPoint::Zero;
		if (Alpha > FFixedPoint::One)  Alpha = FFixedPoint::One;
		CurrentSteer = CurrentSteer + (DesiredSteer - CurrentSteer) * Alpha;
	}

	// -------------------------------------------------------------------
	// 7. Yaw rate: the honest bicycle (w = v/L · tan δ) PLUS — legacy/tail
	//    mode only — the low-speed "helping hand" turn assist. Planned
	//    maneuvers are honest bicycle only: the plan itself provides the
	//    tight-turn capability (arcs, cusps), so assisting would fight it.
	// -------------------------------------------------------------------
	FFixedPoint YawStep = FFixedPoint::Zero;
	if (Wheeled.Wheelbase > FFixedPoint::One)
	{
		const FFixedPoint TanSteer = SeinMath::Tan(CurrentSteer);
		const FFixedPoint YawRate = (CurrentSpeed / Wheeled.Wheelbase) * TanSteer;
		const FFixedPoint MaxRate = MovementData.TurnRate;
		const FFixedPoint ClampedRate = ClampFP(YawRate, -MaxRate, MaxRate);
		YawStep = ClampedRate * DeltaTime;
	}
	if (bAllowAssist
		&& Wheeled.LowSpeedTurnRate > FFixedPoint::Zero
		&& Wheeled.TurnAssistFadeSpeed > FFixedPoint::Epsilon)
	{
		FFixedPoint Fade = FFixedPoint::One - (AbsCurrentSpeed / Wheeled.TurnAssistFadeSpeed);
		if (Fade < FFixedPoint::Zero) Fade = FFixedPoint::Zero;
		if (Fade > FFixedPoint::One)  Fade = FFixedPoint::One;
		// Angle gate: assist only SHARP (u-turn-ish) headings — moderate turns
		// arc honestly from rest instead of pivoting.
		{
			const FFixedPoint HalfPiGate = FFixedPoint::Pi * FFixedPoint::Half;
			FFixedPoint AngleScale = (AbsYawErr - HalfPiGate) / HalfPiGate;
			if (AngleScale < FFixedPoint::Zero) AngleScale = FFixedPoint::Zero;
			if (AngleScale > FFixedPoint::One)  AngleScale = FFixedPoint::One;
			Fade = Fade * AngleScale;
		}
		if (Fade > FFixedPoint::Zero)
		{
			const FFixedPoint AssistMaxStep = Wheeled.LowSpeedTurnRate * Fade * DeltaTime;
			const FFixedPoint AssistStep = ClampFP(YawErrSigned, -AssistMaxStep, AssistMaxStep);
			if (YawErrSigned >= FFixedPoint::Zero)
			{
				if (AssistStep > YawStep)   YawStep = AssistStep;
				if (YawStep > YawErrSigned) YawStep = YawErrSigned;
			}
			else
			{
				if (AssistStep < YawStep)   YawStep = AssistStep;
				if (YawStep < YawErrSigned) YawStep = YawErrSigned;
			}
		}
	}
	const FFixedPoint NewYaw = CurrentYaw + YawStep;
	YawAccumSinceProgress += AbsFP2(YawStep);

	UE_LOG(LogSeinWheeled, Verbose,
		TEXT("Wheeled: pos=(%.1f,%.1f) yaw=%.3f currSteer=%.3f desiredSteer=%.3f speed=%.1f mode=%s seg=%d/%d rev=%d"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(),
		CurrentYaw.ToFloat(), CurrentSteer.ToFloat(), DesiredSteer.ToFloat(),
		CurrentSpeed.ToFloat(),
		bRecovering ? TEXT("recover") : (bManeuverMode ? TEXT("maneuver") : TEXT("pursuit")),
		SegCursor, TailStartSeg, bDriveReverse ? 1 : 0);

	// -------------------------------------------------------------------
	// 8. Orbit backstop — a chassis that has swept >2.5*pi of yaw without any
	//    waypoint/segment progress is circling something; break the loop with
	//    the recovery nudge (the next repath replans the maneuver).
	//    Gated on bManeuverPlanning so the OFF setting is a faithful legacy
	//    A/B control (the recovery machinery did not exist pre-maneuver), and
	//    suppressed near the goal where the action's crowd-stall settle owns
	//    the endgame (a recovery nudge there completes the order mid-nudge
	//    with backward residual velocity).
	// -------------------------------------------------------------------
	if (bRecoveryAllowed && !bRecovering && YawAccumSinceProgress > OrbitYawLimit)
	{
		const bool bEffectiveCanReverse = Wheeled.bCanReverse || MovementData.bCanReverse;
		const FFixedVector Fwd(SeinMath::Cos(CurrentYaw), SeinMath::Sin(CurrentYaw), FFixedPoint::Zero);
		const FFixedPoint ProbeDist = CachedCollisionRadius + FFixedPoint::FromInt(80);
		const FFixedVector Behind(AgentPos.X - Fwd.X * ProbeDist, AgentPos.Y - Fwd.Y * ProbeDist, AgentPos.Z);
		const FFixedVector Ahead(AgentPos.X + Fwd.X * ProbeDist, AgentPos.Y + Fwd.Y * ProbeDist, AgentPos.Z);
		if (bEffectiveCanReverse && IsFootprintPassable(Behind, Nav))      { RecoveryDir = -1; RecoveryTime = RecoverySeconds; }
		else if (IsFootprintPassable(Ahead, Nav))                          { RecoveryDir = +1; RecoveryTime = RecoverySeconds; }
		YawAccumSinceProgress = FFixedPoint::Zero;
		StuckTime = FFixedPoint::Zero;
	}

	// -------------------------------------------------------------------
	// 9. Speed integration + translate along the post-rotation forward.
	// -------------------------------------------------------------------
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		Wheeled.Acceleration, Wheeled.Deceleration, DeltaTime);

	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	// -------------------------------------------------------------------
	// 10. Footprint-aware nav collision floor (honoring an authoritative
	//     cover-slot destination, matching the harness) + ground snap.
	// -------------------------------------------------------------------
	NewPos = ResolveNavCollision(AgentPos, NewPos, Nav,
		Ctx.bAuthoritativeDestination ? &FinalWp : nullptr);
	ApplyGroundSnapAndAltitude(NewPos, Ctx.MovementData, Nav, DeltaTime);

	{
		// Rate-limited pitch/roll smoothing — see comment in SeinInfantryMovement.
		const FFixedPoint TargetPitch = ComputeSlopePitch(NewPos, NewYaw, Nav);
		const FFixedPoint TargetRoll  = ComputeSlopeRoll(NewPos, NewYaw, Nav);
		const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3); // 60°/sec
		MovementData.SmoothedPitch = SmoothAngleToward(MovementData.SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
		MovementData.SmoothedRoll  = SmoothAngleToward(MovementData.SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
		Entity.Transform.Rotation = YawPitchRoll(NewYaw, MovementData.SmoothedPitch, MovementData.SmoothedRoll);
	}
	Entity.Transform.SetLocation(NewPos);
	// Persist velocity along post-rotation forward; bicycle physics couples
	// velocity direction to facing for non-strafing wheeled vehicles.
	// COMMANDED (not post-collision) by design — this keeps vehicles outside
	// the action's hold-escape ladder (whose straight escape legs a min-turn
	// chassis can't drive); the mode's own stuck recovery below fills that role.
	MovementData.Velocity = FFixedVector(CosY * CurrentSpeed, SinY * CurrentSpeed, FFixedPoint::Zero);

	return false;
}
