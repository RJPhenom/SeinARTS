/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTrackedVehicleMovement.cpp
 * @brief   Arc/Pivot tracked controller with tracked-flavored maneuver
 *          planning (straight reverse + momentum U-turn arc; the full word
 *          ladder when an authored MinTurnRadius declares the chassis
 *          non-pivoting). See the header for the design contract.
 */

#include "Movement/SeinTrackedVehicleMovement.h"
// Chassis-generic plan-time toolkit (tangent solves, probes, the full word
// ladder). Lives under the wheeled name for now — renaming to
// SeinVehicleManeuver is deferred until the wheeled PIE pass lands so this
// fork touches no wheeled-named files.
#include "Movement/SeinWheeledManeuver.h"
#include "Movement/SeinMoverHandle.h"
#include "Movement/SeinPlannerHandle.h"
#include "SeinARTSMovementModule.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementComponent.h"
#include "Data/SeinTrackedMovementData.h"
#include "Components/SeinNavigationComponent.h"
#include "Simulation/SeinWorldSubsystem.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Debug/SeinDebugDrawCull.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSeinTracked, Log, All);

namespace
{
	// ------------------------------------------------------------------
	// Compile-time driver constants (determinism rule: never per-run
	// tunables). Values match the wheeled driver where the semantics are
	// shared — DUPLICATED with the geometry helpers below; unify into a
	// shared Movement+ vehicle-driver helper once the wheeled PIE pass
	// lands (this fork touches no wheeled files).
	// ------------------------------------------------------------------
	const FFixedPoint CuspFlipSpeed = FFixedPoint::FromInt(30);
	const FFixedPoint ManeuverLookAhead = FFixedPoint::FromInt(250);
	const FFixedPoint SegmentCloseRadius = FFixedPoint::FromInt(50);
	const FFixedPoint ArcSweepEps = FFixedPoint::Pi / FFixedPoint::FromInt(60); // ~3 deg
	const FFixedPoint StuckSpeedMin       = FFixedPoint::FromInt(50);
	const FFixedPoint StuckTriggerSeconds = FFixedPoint::Half;
	const FFixedPoint RecoverySeconds     = FFixedPoint::FromInt(3) / FFixedPoint::FromInt(5); // 0.6 s
	const FFixedPoint RecoverySpeedCap    = FFixedPoint::FromInt(250);
	const FFixedPoint ManeuverStallAbandonSeconds = FFixedPoint::FromInt(6) / FFixedPoint::FromInt(5); // 1.2 s
	const FFixedPoint OrbitYawLimit = FFixedPoint::Pi * FFixedPoint::FromInt(5) / FFixedPoint::Two; // 2.5*pi
	// Maneuver planner angles: cold engage ~100 deg, in-maneuver hysteresis 45 deg.
	const FFixedPoint ColdEngageAngle     = FFixedPoint::Pi * FFixedPoint::FromInt(100) / FFixedPoint::FromInt(180);
	const FFixedPoint ContinueEngageAngle = FFixedPoint::Pi / FFixedPoint::FromInt(4);
	// Momentum-arc word: reject near-full-circle sweeps; floor the arc radius.
	const FFixedPoint MaxUTurnSweep    = FFixedPoint::Pi * FFixedPoint::FromInt(240) / FFixedPoint::FromInt(180);
	const FFixedPoint MomentumArcMinR  = FFixedPoint::FromInt(100);
	const FFixedPoint MomentumArcMaxR  = FFixedPoint::FromInt(10000);
	// Arc-drive margin: driving an arc at EXACTLY v = TurnRate·R leaves the
	// yaw clamp zero authority to null cross-track error (outward drift
	// becomes the attractor — the chassis spirals off the probed corridor).
	// Cap arc speed at 7/8 of the trackable maximum so the clamp always has
	// ~12% headroom for the radial correction below.
	const FFixedPoint ArcSpeedMarginNum = FFixedPoint::FromInt(7);
	const FFixedPoint ArcSpeedMarginDen = FFixedPoint::FromInt(8);
	// Radial (cross-track) correction: heading bias pulling the chassis back
	// onto the probed circle — full cap at RadialGainDist of offset. Mirrors
	// the wheeled driver's RadTerm (constants match).
	const FFixedPoint RadialGainDist   = FFixedPoint::FromInt(400);
	const FFixedPoint RadialCorrCap    = FFixedPoint::Pi / FFixedPoint::FromInt(12);
	// Straight-reverse word gates (mirror the toolkit's).
	const FFixedPoint StraightReverseMaxTurn = FFixedPoint::Pi / FFixedPoint::FromInt(6); // 30 deg total
	const int32 StraightReverseMaxWaypoints  = 6;

	FFixedPoint AbsFP2(FFixedPoint V) { return V < FFixedPoint::Zero ? -V : V; }
	FFixedPoint MinFP2(FFixedPoint A, FFixedPoint B) { return A < B ? A : B; }
	FFixedPoint MaxFP2(FFixedPoint A, FFixedPoint B) { return A > B ? A : B; }

	/** Robust angular progress along an arc segment (wrap ambiguity resolved
	 *  by splitting the leftover circle). Duplicate of the wheeled driver's. */
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

	/** Segment completion: arcs on ANGULAR progress only (the end-plane test
	 *  false-fires at the start of >=180-deg sweeps and the proximity
	 *  shortcut can exceed a short leg's chord); straights on end-plane
	 *  crossover or proximity. Duplicate of the wheeled driver's. */
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

	/** Wrap-safe far precheck: 32.32 squares wrap past ~46,340 units, so any
	 *  SizeSquared/Size comparison against a small radius must first rule out
	 *  far vectors component-wise. Duplicate of the wheeled driver's. */
	bool IsPlanarFar(const FFixedVector& Delta)
	{
		const FFixedPoint Cap = FFixedPoint::FromInt(20000);
		return AbsFP2(Delta.X) >= Cap || AbsFP2(Delta.Y) >= Cap;
	}

	FFixedPoint PlanarDistSafe(const FFixedVector& A, const FFixedVector& B)
	{
		FFixedVector D = B - A;
		D.Z = FFixedPoint::Zero;
		if (IsPlanarFar(D)) return FFixedPoint::FromInt(100000);
		return D.Size();
	}

	/** First polyline index whose cumulative distance from Pos reaches
	 *  LookAhead (local twin of the toolkit's private join walk). */
	int32 FindJoinIndexLocal(const FFixedVector& Pos, const TArray<FFixedVector>& Waypoints, FFixedPoint LookAhead)
	{
		FFixedPoint Cum = FFixedPoint::Zero;
		FFixedVector Prev = Pos;
		for (int32 i = 0; i < Waypoints.Num(); ++i)
		{
			Cum += PlanarDistSafe(Prev, Waypoints[i]);
			if (Cum >= LookAhead) return i;
			Prev = Waypoints[i];
		}
		return Waypoints.Num() - 1;
	}
}

UScriptStruct* USeinTrackedVehicleMovement::GetMovementDataStruct() const
{
	return FSeinTrackedMovementData::StaticStruct();
}

FFixedPoint USeinTrackedVehicleMovement::GetDeceleration(const FSeinMovementComponent* MovementData) const
{
	const FSeinTrackedMovementData* Data = MovementData ? MovementData->MovementClassData.GetPtr<FSeinTrackedMovementData>() : nullptr;
	return Data ? Data->Deceleration : FSeinTrackedMovementData().Deceleration;
}

FFixedPoint USeinTrackedVehicleMovement::GetMinTurnRadius(const FSeinMovementComponent* MovementData) const
{
	// Read MinTurnRadius from the tracked-specific sub-data slot on the
	// movement component, falling back to 0 (the "always pivot at sharp
	// corners" default) when no sub-data is authored.
	if (!MovementData) return FFixedPoint::Zero;
	if (const FSeinTrackedMovementData* Tracked = MovementData->MovementClassData.GetPtr<FSeinTrackedMovementData>())
	{
		return Tracked->MinTurnRadius;
	}
	return FFixedPoint::Zero;
}

FFixedPoint USeinTrackedVehicleMovement::StepYawWithInertia(FFixedPoint YawDelta, FFixedPoint TurnRate,
	FFixedPoint TurnAccel, FFixedPoint Dt)
{
	const FFixedPoint MaxStep = TurnRate * Dt;
	const FFixedPoint InstantStep = ClampFP(YawDelta, -MaxStep, MaxStep);
	// OFF (default): the pre-inertia behavior, bit-exact — rate state untouched.
	if (TurnAccel <= FFixedPoint::Zero || Dt <= FFixedPoint::Epsilon) return InstantStep;

	// Rate the demand asks for this tick (close the whole error), capped at
	// TurnRate; the RATE slews toward it at TurnAccel. Opposite-sign demands
	// decelerate through zero first — that's the inertia showing, by design.
	const FFixedPoint DesiredRate = ClampFP(YawDelta / Dt, -TurnRate, TurnRate);
	const FFixedPoint MaxRateStep = TurnAccel * Dt;
	CurrentYawRate = CurrentYawRate + ClampFP(DesiredRate - CurrentYawRate, -MaxRateStep, MaxRateStep);
	FFixedPoint Step = CurrentYawRate * Dt;
	// Same-direction overshoot clamp: never turn PAST the demand in one tick
	// (and bleed the wound-up rate down with it, so there is no ringing).
	// Applied only when there IS a demand — a zero-demand tick is a pure
	// eased settle (the residual rate decays toward 0 above and its step is
	// applied as-is, the whole point of the inertia).
	if (YawDelta > FFixedPoint::Zero)
	{
		if (Step > YawDelta) { Step = YawDelta; CurrentYawRate = DesiredRate; }
	}
	else if (YawDelta < FFixedPoint::Zero)
	{
		if (Step < YawDelta) { Step = YawDelta; CurrentYawRate = DesiredRate; }
	}
	return Step;
}

void USeinTrackedVehicleMovement::ResetDriverState()
{
	SegCursor = 0;
	TailStartSeg = 0;
	bDriveReverseLatch = false;
	CurrentYawRate = FFixedPoint::Zero;
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

void USeinTrackedVehicleMovement::OnMoveEnd(FSeinEntity& Entity)
{
	Super::OnMoveEnd(Entity);
	// The NEXT order's initial PlanPath runs BEFORE OnMoveBegin's reset — the
	// engage-hysteresis / recovery reads must not see the finished order's
	// state. NOTE: the action calls OnMoveEnd only on COMPLETED orders
	// (OnCancel/OnFail skip it — base gap), so PlanPath's hysteresis read is
	// additionally destination-gated rather than trusting this reset alone.
	ResetDriverState();
	bIsReversing = false;
}

void USeinTrackedVehicleMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// Base dispatcher: fires BP_OnMoveBegin for BP subclasses and re-runs the
	// per-order tuning hydration (both were silently skipped before).
	Super::OnMoveBegin(Ctx);

	if (!Ctx.MovementData) return;

	FSeinEntity& Entity = Ctx.Entity;
	const FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;

	// Preserve MovementData.Velocity so reorders carry momentum.
	ResetDriverState();
	bIsReversing = false;

	// LEGACY one-shot auto-reverse latch — only when maneuver planning is off
	// (the planner expresses reverse as typed segments, re-decided per repath).
	const FSeinTrackedMovementData DefaultsTracked;
	const FSeinTrackedMovementData* TrackedPtr = MovementData.MovementClassData.GetPtr<FSeinTrackedMovementData>();
	const FSeinTrackedMovementData& Tracked = TrackedPtr ? *TrackedPtr : DefaultsTracked;
	if (!Tracked.bManeuverPlanning)
	{
		const int32 N = Path.Waypoints.Num();
		bIsReversing = (N > 0) && ShouldAutoReverse(
			Entity.Transform.GetLocation(),
			Entity.Transform.Rotation,
			Path.Waypoints[N - 1],
			MovementData);
	}
}

FSeinMotion USeinTrackedVehicleMovement::ComputeArrivalMotion_Implementation(USeinMoverHandle* Mover)
{
	// Roll-through arrival: keep the (already kinematically-braked) residual
	// velocity; the idle coast-down finishes the stop through GetDeceleration.
	FSeinMotion Motion;
	const FSeinMovementContext* C = Mover ? Mover->GetContext() : nullptr;
	if (C && C->MovementData)
	{
		Motion.Velocity = C->MovementData->Velocity;
	}
	return Motion;
}

bool USeinTrackedVehicleMovement::RefreshPathCache(const FSeinPath& Path, FFixedPoint CurrentSpeed, FFixedPoint CuspFlipSpd)
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

	TailStartSeg = 0;
	for (int32 i = 0; i < SegNum; ++i)
	{
		const FSeinPathSegment& S = Path.Segments[i];
		if (S.Type != ESeinPathSegmentType::Straight || S.bReverse) TailStartSeg = i + 1;
	}
	SegCursor = 0;
	const bool bFirstReverse = (TailStartSeg > 0 && SegNum > 0) ? Path.Segments[0].bReverse : false;
	bDriveReverseLatch = (AbsFP2(CurrentSpeed) > CuspFlipSpd) ? (CurrentSpeed < FFixedPoint::Zero) : bFirstReverse;
	YawAccumSinceProgress = FFixedPoint::Zero;
	LastProgressWaypointIndex = -1;
	return true;
}

ESeinPathResult USeinTrackedVehicleMovement::PlanPath(const FSeinPlanPathContext& Ctx, FSeinPath& OutPath) const
{
	const ESeinPathResult Result = Super::PlanPath(Ctx, OutPath);
	if (Result != ESeinPathResult::Found || !Ctx.MovementData) return Result;
	if (BypassPathfinding()) return Result;
	if (OutPath.Waypoints.Num() == 0) return Result;
	// A path that already carries typed segments was authored by a BP Plan
	// Path override — never clobber it with a re-fit.
	if (OutPath.HasTypedSegments()) return Result;

	const FSeinTrackedMovementData DefaultsTracked;
	const FSeinTrackedMovementData* TrackedPtr = Ctx.MovementData->MovementClassData.GetPtr<FSeinTrackedMovementData>();
	const FSeinTrackedMovementData& Tracked = TrackedPtr ? *TrackedPtr : DefaultsTracked;
	if (!Tracked.bManeuverPlanning) return Result;

	const FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FFixedPoint RevTop = (MovementData.ReverseTopSpeed > FFixedPoint::Zero)
		? MovementData.ReverseTopSpeed : MovementData.TopSpeed * FFixedPoint::Half;

	SeinWheeledManeuver::FInputs In;
	In.Pos = Ctx.Entity.Transform.GetLocation();
	In.Yaw = YawFromRotation(Ctx.Entity.Transform.Rotation);
	In.FootprintRadius = ResolveCollisionRadius(Ctx.World, Ctx.SelfHandle, Ctx.NavData);
	In.NavLayerMask = Ctx.NavData ? Ctx.NavData->NavLayerMask : 0x01;
	In.ReverseSpeedPenalty = (RevTop > FFixedPoint::Epsilon)
		? MaxFP2(FFixedPoint::One, MovementData.TopSpeed / RevTop) : FFixedPoint::Two;
	In.ForwardPathBias = MaxFP2(FFixedPoint::One, Tracked.ForwardPathBias);
	In.ReverseEngageDistance = MovementData.ReverseEngageDistanceThreshold;
	In.ReverseEngageDot = MovementData.ReverseEngageDotThreshold;
	In.ReversePlanMaxDistance = Tracked.ReversePlanMaxDistance;
	In.bCanReverse = Tracked.bCanReverse || MovementData.bCanReverse;
	{
		const FFixedVector Fwd = Ctx.Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
		const FFixedPoint VelDot = MovementData.Velocity.X * Fwd.X + MovementData.Velocity.Y * Fwd.Y;
		const bool bRecoveryReverse = RecoveryTime > FFixedPoint::Zero && RecoveryDir < 0;
		In.bCurrentlyReversing = !bRecoveryReverse
			&& (VelDot < FFixedPoint::Zero)
			&& MovementData.Velocity.SizeSquared() > CuspFlipSpeed * CuspFlipSpeed;
	}
	In.Nav = Ctx.Nav;
	// Engage hysteresis, RESET-SAFE: OnMoveEnd fires only on COMPLETED orders
	// (the action's OnCancel/OnFail skip it — base gap, shared with wheeled),
	// so a cancelled-mid-maneuver order could leak a stale in-maneuver flag
	// into the next order's initial plan. Gate the hysteresis on the plan
	// request targeting the SAME destination as the cached in-flight path
	// (byte-exact) — a reissue to a new goal always plans cold.
	const bool bSameDestination =
		Ctx.Destination.X == CachedPathLastWp.X
		&& Ctx.Destination.Y == CachedPathLastWp.Y
		&& Ctx.Destination.Z == CachedPathLastWp.Z;
	const bool bManeuverActive = bSameDestination && TailStartSeg > 0 && SegCursor < TailStartSeg;

	// ------------------------------------------------------------------
	// AUTHORED-RADIUS chassis (MinTurnRadius > 0 = declared non-pivoting):
	// run the full shared word ladder, exactly like wheeled.
	// ------------------------------------------------------------------
	SeinWheeledManeuver::FPlan Plan;
	bool bHavePlan = false;
	if (Tracked.MinTurnRadius > FFixedPoint::Zero)
	{
		In.MinTurnRadius = Tracked.MinTurnRadius;
		In.CruiseTurnRadius = (MovementData.TurnRate > FFixedPoint::Epsilon)
			? MinFP2(MaxFP2(Tracked.MinTurnRadius, MovementData.TopSpeed / MovementData.TurnRate), MomentumArcMaxR)
			: Tracked.MinTurnRadius;
		if (bManeuverActive) { In.EngageAngle = ContinueEngageAngle; }
		bHavePlan = SeinWheeledManeuver::PlanStartManeuver(In, OutPath.Waypoints, Plan);
	}
	else
	{
		// --------------------------------------------------------------
		// NEUTRAL-STEER chassis: pivoting covers slow misalignment for
		// free, so only two words earn a plan (local builders — the
		// toolkit's candidate internals are file-private; unify when the
		// toolkit is renamed post-PIE):
		//   B  — straight reverse for a close behind-goal (segment-native
		//        replacement for the old whole-order latch);
		//   A' — momentum U-turn arc when ALREADY AT SPEED (R = v/TurnRate),
		//        so an at-speed turnaround carves instead of braking to a
		//        pivot. Slow chassis: no plan — pivot mode is correct.
		// --------------------------------------------------------------
		const TArray<FFixedVector>& W = OutPath.Waypoints;
		const int32 N = W.Num();
		const FFixedPoint Speed = MovementData.Velocity.Size(); // bounded by TopSpeed — wrap-safe
		const FFixedPoint EngageAngle = bManeuverActive ? ContinueEngageAngle : ColdEngageAngle;

		// Shared join-direction / heading-error setup.
		const FFixedPoint ArcR = (MovementData.TurnRate > FFixedPoint::Epsilon)
			? MinFP2(MaxFP2(Speed / MovementData.TurnRate, MomentumArcMinR), MomentumArcMaxR)
			: FFixedPoint::Zero;
		const int32 JoinIdx = FindJoinIndexLocal(In.Pos, W,
			MaxFP2(ArcR * FFixedPoint::Two, FFixedPoint::FromInt(600)));
		const FFixedVector J = W[JoinIdx];
		FFixedVector ToJ = J - In.Pos;
		ToJ.Z = FFixedPoint::Zero;
		FFixedPoint AbsErr = FFixedPoint::Zero;
		FFixedPoint TurnSign = FFixedPoint::One;
		if (ToJ.SizeSquared() > FFixedPoint::Epsilon && !IsPlanarFar(ToJ))
		{
			const FFixedPoint Err = SeinWheeledManeuver::WrapSigned(
				SeinMath::Atan2(ToJ.Y, ToJ.X) - In.Yaw);
			AbsErr = AbsFP2(Err);
			TurnSign = (Err >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
		}

		// A' — momentum arc. Gate: already at speed (well above the pivot
		// band — below it, braking to a pivot IS the tracked-correct move).
		SeinWheeledManeuver::FPlan ArcPlan;
		FFixedPoint ArcCost = FFixedPoint::Zero;
		bool bArcValid = false;
		const FFixedPoint ArcPlanMinSpeed = MaxFP2(Tracked.PivotSpeed * FFixedPoint::Two,
			MovementData.TopSpeed / FFixedPoint::FromInt(4));
		// !bCurrentlyReversing: |Velocity| is direction-agnostic — a chassis
		// BACKING at speed must not plan a forward "momentum" arc it would
		// have to brake-to-cusp into from a pose it has already left; the
		// reverse word and the pivot fallback own that case.
		if (ArcR > FFixedPoint::Zero && Speed >= ArcPlanMinSpeed && AbsErr >= EngageAngle
			&& !In.bCurrentlyReversing)
		{
			FFixedVector Center, Depart;
			FFixedPoint Sweep = FFixedPoint::Zero;
			if (SeinWheeledManeuver::SolveTangentArc(In.Pos, In.Yaw, TurnSign, ArcR, J, Center, Depart, Sweep)
				&& AbsFP2(Sweep) <= MaxUTurnSweep)
			{
				const FFixedPoint Phi0 = SeinMath::Atan2(In.Pos.Y - Center.Y, In.Pos.X - Center.X);
				if (SeinWheeledManeuver::ProbeArcClear(In, Center, ArcR, Phi0, Sweep)
					&& SeinWheeledManeuver::ProbeStraightClear(In, Depart, J))
				{
					SeinWheeledManeuver::FLeg Arc;
					Arc.bArc = true;
					Arc.From = In.Pos;
					Arc.To = Depart;
					Arc.Center = Center;
					Arc.Radius = ArcR;
					Arc.Sweep = Sweep;
					Arc.Length = ArcR * AbsFP2(Sweep);
					SeinWheeledManeuver::FLeg Join;
					Join.From = Depart;
					Join.To = J;
					Join.Length = PlanarDistSafe(Depart, J);
					ArcPlan.Legs.Add(Arc);
					ArcPlan.Legs.Add(Join);
					ArcPlan.JoinWaypointIndex = JoinIdx;
					ArcPlan.HeadCost = Arc.Length + Join.Length;
					ArcCost = (ArcPlan.HeadCost + SeinWheeledManeuver::PolylineLengthFrom(W, JoinIdx))
						/ In.ForwardPathBias; // forward word gets the bias advantage
					bArcValid = true;
				}
			}
		}

		// B — straight reverse (close behind-goal, near-straight path).
		SeinWheeledManeuver::FPlan RevPlan;
		FFixedPoint RevCost = FFixedPoint::Zero;
		bool bRevValid = false;
		if (In.bCanReverse && N > 0 && N <= StraightReverseMaxWaypoints)
		{
			const FFixedPoint Dist = PlanarDistSafe(In.Pos, W[N - 1]);
			FFixedVector ToFinal = W[N - 1] - In.Pos;
			ToFinal.Z = FFixedPoint::Zero;
			const FFixedVector Fwd(SeinMath::Cos(In.Yaw), SeinMath::Sin(In.Yaw), FFixedPoint::Zero);
			const FFixedVector ToFinalN = FFixedVector::GetSafeNormal(ToFinal);
			const bool bBehind = (Fwd.X * ToFinalN.X + Fwd.Y * ToFinalN.Y) <= In.ReverseEngageDot;
			if (Dist > FFixedPoint::Epsilon && Dist <= In.ReverseEngageDistance && bBehind)
			{
				// Near-straight gate + chain-preserving reverse legs (micro-hops
				// absorbed; terminal exact).
				FFixedPoint TotalTurn = FFixedPoint::Zero;
				FFixedVector Prev = In.Pos;
				FFixedPoint PrevDir = FFixedPoint::Zero;
				bool bHavePrevDir = false;
				for (int32 i = 0; i < N; ++i)
				{
					FFixedVector Seg = W[i] - Prev;
					Seg.Z = FFixedPoint::Zero;
					if (Seg.SizeSquared() > FFixedPoint::Epsilon)
					{
						const FFixedPoint Dir = SeinMath::Atan2(Seg.Y, Seg.X);
						if (bHavePrevDir) TotalTurn += AbsFP2(SeinWheeledManeuver::WrapSigned(Dir - PrevDir));
						PrevDir = Dir;
						bHavePrevDir = true;
					}
					Prev = W[i];
				}
				if (TotalTurn <= StraightReverseMaxTurn
					&& SeinWheeledManeuver::ProbeStraightClear(In, In.Pos, W[0]))
				{
					Prev = In.Pos;
					for (int32 i = 0; i < N; ++i)
					{
						if (PlanarDistSafe(Prev, W[i]) > FFixedPoint::One)
						{
							SeinWheeledManeuver::FLeg Leg;
							Leg.bReverse = true;
							Leg.From = Prev;
							Leg.To = W[i];
							Leg.Length = PlanarDistSafe(Prev, W[i]);
							RevPlan.Legs.Add(Leg);
							Prev = W[i];
						}
						else if (i == N - 1 && RevPlan.Legs.Num() > 0)
						{
							SeinWheeledManeuver::FLeg& Last = RevPlan.Legs.Last();
							Last.To = W[i];
							Last.Length = PlanarDistSafe(Last.From, W[i]);
							Prev = W[i];
						}
					}
					if (RevPlan.Legs.Num() > 0)
					{
						RevPlan.JoinWaypointIndex = -1;
						for (const SeinWheeledManeuver::FLeg& L : RevPlan.Legs)
						{
							RevPlan.HeadCost += L.Length * In.ReverseSpeedPenalty;
						}
						RevCost = RevPlan.HeadCost;
						bRevValid = true;
					}
				}
			}
		}

		// Pick: reverse continuity first, then biased cost.
		if (In.bCurrentlyReversing && bRevValid) { Plan = RevPlan; bHavePlan = true; }
		else if (bArcValid && bRevValid) { Plan = (ArcCost <= RevCost) ? ArcPlan : RevPlan; bHavePlan = true; }
		else if (bArcValid) { Plan = ArcPlan; bHavePlan = true; }
		else if (bRevValid) { Plan = RevPlan; bHavePlan = true; }
	}

	if (!bHavePlan) return Result;

	// Emit through the planner handle (Super unbound it — rebind with the
	// same localized-const_cast idiom the base uses).
	const TArray<FFixedVector> Coarse = OutPath.Waypoints;
	const bool bWasPartial = OutPath.bIsPartial;
	USeinTrackedVehicleMovement* MutableThis = const_cast<USeinTrackedVehicleMovement*>(this);
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
		for (int32 i = Plan.JoinWaypointIndex; i + 1 < Coarse.Num(); ++i)
		{
			Handle->AddStraightSegment(Coarse[i], Coarse[i + 1], false);
		}
	}
	Handle->FinalizeTypedPath(bWasPartial);
	Handle->SetContext(nullptr, nullptr);
	return ESeinPathResult::Found;
}

bool USeinTrackedVehicleMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;

	const FSeinTrackedMovementData DefaultsTracked;
	const FSeinTrackedMovementData* TrackedPtr = MovementData.MovementClassData.GetPtr<FSeinTrackedMovementData>();
	const FSeinTrackedMovementData& Tracked = TrackedPtr ? *TrackedPtr : DefaultsTracked;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadiusSq = Ctx.AcceptanceRadiusSq;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	const int32 N = Path.Waypoints.Num();
	if (N == 0) return true;

	// -------------------------------------------------------------------
	// 1. Recover signed scalar speed from persisted velocity vector.
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
	// 2. Path identity + segment cursor (rebuilds on initial plan and every
	//    repath), then geometric cursor advance.
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

	bool bDriveReverse;
	if (bManeuverMode)         { bDriveReverse = bDriveReverseLatch; }
	else if (bHasManeuverHead) { bDriveReverse = false; bDriveReverseLatch = false; }
	else                       { bDriveReverse = bIsReversing; }

	// -------------------------------------------------------------------
	// 3. Arrival check — far-prechecked (fixed-point squares wrap past
	//    ~463 m), reverse-aware overshoot (travel heading), overshoot
	//    suppressed while a planned maneuver circles back, and routed
	//    through DispatchArrivalMotion (the Tier-2 contract).
	// -------------------------------------------------------------------
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		if (!IsPlanarFar(ToFinal))
		{
			const bool bWithinAcceptance = ToFinal.SizeSquared() <= AcceptanceRadiusSq;
			const FFixedPoint VicinityRadiusSq = AcceptanceRadiusSq * FFixedPoint::FromInt(4);
			const FFixedPoint OvershootSpeedCap = MovementData.TopSpeed / FFixedPoint::FromInt(3);
			const FFixedQuaternion TravelRot = bDriveReverse
				? YawOnly(CurrentYaw + FFixedPoint::Pi) : EntryRot;
			const bool bOvershoot = !bManeuverMode && IsOvershootArrival(
				AgentPos, FinalWp, TravelRot,
				CurrentSpeed, VicinityRadiusSq, OvershootSpeedCap);
			if (bWithinAcceptance || bOvershoot)
			{
				UE_LOG(LogSeinTracked, Verbose,
					TEXT("Tracked arrival: within=%d overshoot=%d speed=%.2f"),
					bWithinAcceptance ? 1 : 0, bOvershoot ? 1 : 0, CurrentSpeed.ToFloat());
				DispatchArrivalMotion(Ctx);
				return true;
			}
		}
	}

	// -------------------------------------------------------------------
	// 4. Waypoint advance — the harness helper (incoming-direction crossover
	//    + distance fallback). The old hand-rolled distance-only loop missed
	//    crossover on overshoot-at-speed, producing the backward-carrot spin.
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

	// Final-approach kinematic cap toward the acceptance-ring EDGE (pairs
	// with the roll-through arrival) — far-guarded like the arrival test.
	FFixedPoint MaxArrivalSpeed = FFixedPoint::FromInt(1000000);
	FFixedPoint DistFinal = FFixedPoint::FromInt(100000);
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		if (!IsPlanarFar(ToFinal))
		{
			DistFinal = ToFinal.Size();
			const FFixedPoint Acceptance = Ctx.NavData ? Ctx.NavData->AcceptanceRadius : FFixedPoint::Zero;
			const FFixedPoint BrakeDist = (DistFinal > Acceptance)
				? (DistFinal - Acceptance) : FFixedPoint::Zero;
			MaxArrivalSpeed = KinematicArrivalSpeedCap(BrakeDist, Tracked.Deceleration);
		}
	}

	// -------------------------------------------------------------------
	// 4.5 Stuck detection — entry-to-entry displacement (includes PostTick
	//     collision pushes → crowd pins register). Gated on bManeuverPlanning
	//     (legacy A/B parity) and suppressed near the goal.
	// -------------------------------------------------------------------
	const FFixedPoint NearGoalSuppress = CachedCollisionRadius + FFixedPoint::FromInt(150);
	const bool bRecoveryAllowed = Tracked.bManeuverPlanning && DistFinal > NearGoalSuppress;
	if (bRecoveryAllowed && !bRecovering && bLastEntryPosValid)
	{
		FFixedVector EntryMoved = AgentPos - LastEntryPos;
		EntryMoved.Z = FFixedPoint::Zero;
		const FFixedPoint Expected = AbsCurrentSpeed * DeltaTime;
		if (AbsCurrentSpeed > StuckSpeedMin && Expected > FFixedPoint::One
			&& !IsPlanarFar(EntryMoved)
			&& EntryMoved.Size() < Expected * FFixedPoint::Half * FFixedPoint::Half)
		{
			StuckTime += DeltaTime;
			if (StuckTime >= StuckTriggerSeconds)
			{
				const bool bEffectiveCanReverse = Tracked.bCanReverse || MovementData.bCanReverse;
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
	// 5. Steering: desired yaw + translation gating, by mode. Tracked yaw is
	//    direct (clamp to TurnRate·dt) — no steer state. The ARC/PIVOT split
	//    gates TRANSLATION everywhere, which makes cusps nearly free: a
	//    stopped chassis at a cusp simply pivots to the flipped leg's heading
	//    before driving.
	// -------------------------------------------------------------------
	FFixedPoint NewYaw = CurrentYaw;
	FFixedPoint TargetSpeed = FFixedPoint::Zero;
	FFixedPoint AppliedTurnAbs = FFixedPoint::Zero;
#if UE_ENABLE_DEBUG_DRAWING
	FFixedVector DebugSteerTarget = AgentPos;
	bool bDebugSteerTargetValid = false;
#endif

	if (bRecovering)
	{
		RecoveryTime -= DeltaTime;
		if (RecoveryTime <= FFixedPoint::Zero) { RecoveryDir = 0; }
		const FFixedPoint Mag = MinFP2(RecoverySpeedCap, (RecoveryDir < 0) ? RevTop : Cruise);
		TargetSpeed = (RecoveryDir < 0) ? -Mag : Mag;
		bDriveReverse = RecoveryDir < 0;
		// No steering demand — let any residual hull rotation ease out
		// through the inertia slew instead of stopping dead (no-op when
		// TurnAcceleration is 0).
		const FFixedPoint Residual = StepYawWithInertia(FFixedPoint::Zero,
			MovementData.TurnRate, Tracked.TurnAcceleration, DeltaTime);
		NewYaw = CurrentYaw + Residual;
		AppliedTurnAbs = AbsFP2(Residual);
	}
	else if (bManeuverMode)
	{
		const FSeinPathSegment& S = Path.Segments[SegCursor];

		// Cusp gate: brake under the epsilon, flip the latch; the pivot mode
		// below then rotates the stopped chassis onto the flipped leg.
		if (S.bReverse != bDriveReverseLatch)
		{
			if (AbsCurrentSpeed > CuspFlipSpeed)
			{
				TargetSpeed = FFixedPoint::Zero; // brake to the cusp, hold heading
				// Ease residual rotation out through the inertia slew
				// (no-op when TurnAcceleration is 0).
				const FFixedPoint Residual = StepYawWithInertia(FFixedPoint::Zero,
					MovementData.TurnRate, Tracked.TurnAcceleration, DeltaTime);
				NewYaw = CurrentYaw + Residual;
				AppliedTurnAbs = AbsFP2(Residual);
			}
			else
			{
				bDriveReverseLatch = S.bReverse;
			}
			bDriveReverse = bDriveReverseLatch;
		}

		// Hoisted so the stall-escape's pivot exemption below reads the REAL
		// alignment state, not a scope-local. Effective pivot band bound:
		// PivotSpeed can be authored below the compile-time CuspFlipSpeed,
		// which would let a freshly-flipped cusp leg creep-drive up to 180°
		// misaligned — the max() keeps the cusp contract ("the stopped
		// chassis pivots to the flipped leg's heading") authoring-proof.
		FFixedPoint AlignDotPostTurn = FFixedPoint::One;
		const FFixedPoint PivotBound = MaxFP2(Tracked.PivotSpeed, CuspFlipSpeed);
		if (S.bReverse == bDriveReverseLatch)
		{
			FFixedPoint DesiredYaw = CurrentYaw;
			bool bHaveDesired = false;
			FFixedPoint SegSpeedMag;
			FFixedPoint DistToSegEnd;
			if (S.Type == ESeinPathSegmentType::Arc && S.Radius > FFixedPoint::Epsilon)
			{
				const FFixedPoint SweepSign = (S.SweepAngle >= FFixedPoint::Zero) ? FFixedPoint::One : -FFixedPoint::One;
				FFixedVector FromC = AgentPos - S.Center;
				FromC.Z = FFixedPoint::Zero;
				const FFixedPoint Phi = SeinMath::Atan2(FromC.Y, FromC.X);
				const FFixedPoint TangentYaw = Phi + SweepSign * FFixedPoint::Pi * FFixedPoint::Half;
				// Radial correction: rotate the desired heading toward/away
				// from the center so displacement off the probed circle
				// (collision pushes, plan-pose staleness) converges back
				// instead of persisting — the same sign for forward and
				// reverse legs (rotating the nose rotates the travel
				// direction identically).
				const FFixedPoint RadialErr = FromC.Size() - S.Radius; // >0 = outside
				const FFixedPoint RadCorr = SweepSign
					* ClampFP(RadialErr / RadialGainDist, -RadialCorrCap, RadialCorrCap);
				DesiredYaw = (S.bReverse ? TangentYaw + FFixedPoint::Pi : TangentYaw) + RadCorr;
				bHaveDesired = true;
				SegSpeedMag = S.bReverse ? RevTop : Cruise;
				if (MovementData.TurnRate > FFixedPoint::Epsilon)
				{
					// 7/8 margin: see the constants block — full TurnRate·R
					// leaves the yaw clamp no authority to apply RadCorr.
					SegSpeedMag = MinFP2(SegSpeedMag,
						MovementData.TurnRate * S.Radius * ArcSpeedMarginNum / ArcSpeedMarginDen);
				}
				DistToSegEnd = (AbsFP2(S.SweepAngle) - ArcProgress(S, AgentPos)) * S.Radius;
#if UE_ENABLE_DEBUG_DRAWING
				DebugSteerTarget = S.To;
				bDebugSteerTargetValid = true;
#endif
			}
			else
			{
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
					DesiredYaw = S.bReverse
						? SeinMath::Atan2(-ToTarget.Y, -ToTarget.X)
						: SeinMath::Atan2(ToTarget.Y, ToTarget.X);
					bHaveDesired = true;
				}
				SegSpeedMag = S.bReverse ? RevTop : Cruise;
#if UE_ENABLE_DEBUG_DRAWING
				DebugSteerTarget = Carrot;
				bDebugSteerTargetValid = true;
#endif
			}

			// Turn toward the leg heading at TurnRate; translation is gated by
			// the PIVOT split below (a misaligned slow chassis pivots first).
			if (bHaveDesired)
			{
				const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
				const FFixedPoint AppliedTurn = StepYawWithInertia(YawDelta,
					MovementData.TurnRate, Tracked.TurnAcceleration, DeltaTime);
				NewYaw = CurrentYaw + AppliedTurn;
				AppliedTurnAbs = AbsFP2(AppliedTurn);
				const FFixedPoint RemainingErr = AbsFP2(SeinWheeledManeuver::WrapSigned(DesiredYaw - NewYaw));
				AlignDotPostTurn = SeinMath::Cos(RemainingErr);
			}

			// Anticipatory braking into the next segment's entry speed.
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
					NextEntry = MinFP2(Next.bReverse ? RevTop : Cruise,
						MovementData.TurnRate * Next.Radius * ArcSpeedMarginNum / ArcSpeedMarginDen);
				}
				else
				{
					NextEntry = Next.bReverse ? RevTop : Cruise;
				}
				if (NextEntry < SegSpeedMag && Tracked.Deceleration > FFixedPoint::Zero)
				{
					const FFixedPoint CapSq = NextEntry * NextEntry
						+ FFixedPoint::Two * Tracked.Deceleration * MaxFP2(DistToSegEnd, FFixedPoint::Zero);
					SegSpeedMag = MinFP2(SegSpeedMag, SeinMath::Sqrt(CapSq));
				}
			}

			if (MaxArrivalSpeed < SegSpeedMag) SegSpeedMag = MaxArrivalSpeed;
			// Maneuver legs yield to avoidance by BRAKING only (bending a
			// planned leg breaks its geometry).
			SegSpeedMag = SegSpeedMag * SpeedYield;

			// PIVOT gate on translation: slow + misaligned = stand and turn.
			// PivotBound (not raw PivotSpeed) so a cusp flip always lands
			// inside a live pivot band.
			if (AbsCurrentSpeed <= PivotBound && AlignDotPostTurn < Tracked.PivotAlignDot)
			{
				SegSpeedMag = FFixedPoint::Zero;
			}
			TargetSpeed = S.bReverse ? -SegSpeedMag : SegSpeedMag;
			bDriveReverse = S.bReverse;
		}

		// Maneuver traffic-stall escape, jittered per entity (handle % 8 —
		// congruent indices still collide; rare, and the pair resolves via
		// the differing replans that follow). Excludes cusp braking AND a
		// genuine in-progress pivot. The pivot exemption gates on the REAL
		// alignment state (AlignDotPostTurn vs PivotAlignDot) — an
		// AppliedTurn-magnitude test would never expire, because the yaw
		// round-trip through the quaternion + LUT trig leaves ~1e-3 rad of
		// residual turn every tick even at perfect convergence.
		const FSeinPathSegment& SCur = Path.Segments[SegCursor];
		const bool bCuspBraking = SCur.bReverse != bDriveReverseLatch;
		const bool bPivoting = AbsCurrentSpeed <= PivotBound && AlignDotPostTurn < Tracked.PivotAlignDot;
		if (!bCuspBraking && !bPivoting && AbsCurrentSpeed < StuckSpeedMin)
		{
			ManeuverStallTime += DeltaTime;
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
		// LEGACY / TAIL: the classic arc/pivot controller (pre-maneuver
		// behavior), plus the avoidance SpeedScale yield.
		// ---------------------------------------------------------------
		const FFixedPoint LookAheadFloor = (Tracked.LookAheadDistance > FFixedPoint::Zero)
			? Tracked.LookAheadDistance : FFixedPoint::FromInt(100);
		const FFixedPoint LookAhead = ComputeAdaptiveLookAhead(
			LookAheadFloor, Tracked.LookAheadTimeHorizon, AbsCurrentSpeed);
		const FFixedVector LookAheadPoint = ResolveLookAheadPoint(
			AgentPos, Path, CurrentWaypointIndex, LookAhead);

		FFixedVector ToTarget = LookAheadPoint - AgentPos;
		ToTarget.Z = FFixedPoint::Zero;
		if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
		{
			ToTarget = ApplyAvoidanceSteer(Ctx, FFixedVector::GetSafeNormal(ToTarget));
		}
#if UE_ENABLE_DEBUG_DRAWING
		DebugSteerTarget = LookAheadPoint;
		bDebugSteerTargetValid = true;
#endif

		// Reversing flips the steering target — the BACK points at the goal.
		const FFixedVector EffectiveToTarget = bDriveReverse ? -ToTarget : ToTarget;

		FFixedPoint AbsYawErr = FFixedPoint::Zero;
		FFixedPoint AlignDotPostTurn = FFixedPoint::One;
		if (EffectiveToTarget.SizeSquared() > FFixedPoint::Epsilon)
		{
			const FFixedPoint DesiredYaw = SeinMath::Atan2(EffectiveToTarget.Y, EffectiveToTarget.X);
			const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
			AbsYawErr = AbsFP2(YawDelta);
			const FFixedPoint AppliedTurn = StepYawWithInertia(YawDelta,
				MovementData.TurnRate, Tracked.TurnAcceleration, DeltaTime);
			NewYaw = CurrentYaw + AppliedTurn;
			AppliedTurnAbs = AbsFP2(AppliedTurn);
			const FFixedVector ToTargetN = FFixedVector::GetSafeNormal(EffectiveToTarget);
			const FFixedPoint NewFwdX = SeinMath::Cos(NewYaw);
			const FFixedPoint NewFwdY = SeinMath::Sin(NewYaw);
			AlignDotPostTurn = NewFwdX * ToTargetN.X + NewFwdY * ToTargetN.Y;
		}

		// ARC vs PIVOT throttle (unchanged legacy semantics).
		FFixedPoint ThrottleScale = FFixedPoint::One;
		if (AbsCurrentSpeed > Tracked.PivotSpeed)
		{
			if (Tracked.SharpTurnBrakeStrength > FFixedPoint::Zero
				&& Tracked.SharpTurnBrakeAngle < FFixedPoint::Pi
				&& AbsYawErr > Tracked.SharpTurnBrakeAngle)
			{
				const FFixedPoint SharpRange = FFixedPoint::Pi - Tracked.SharpTurnBrakeAngle;
				if (SharpRange > FFixedPoint::Epsilon)
				{
					FFixedPoint AngleT = (AbsYawErr - Tracked.SharpTurnBrakeAngle) / SharpRange;
					if (AngleT > FFixedPoint::One) AngleT = FFixedPoint::One;
					FFixedPoint SpeedT = FFixedPoint::One;
					if (MovementData.TopSpeed > FFixedPoint::Epsilon)
					{
						SpeedT = AbsCurrentSpeed / MovementData.TopSpeed;
						if (SpeedT > FFixedPoint::One)  SpeedT = FFixedPoint::One;
						if (SpeedT < FFixedPoint::Zero) SpeedT = FFixedPoint::Zero;
					}
					const FFixedPoint Reduction = Tracked.SharpTurnBrakeStrength * AngleT * SpeedT;
					ThrottleScale = FFixedPoint::One - Reduction;
					if (ThrottleScale < FFixedPoint::Zero) ThrottleScale = FFixedPoint::Zero;
				}
			}
		}
		else
		{
			ThrottleScale = (AlignDotPostTurn < Tracked.PivotAlignDot)
				? FFixedPoint::Zero
				: FFixedPoint::One;
		}

		FFixedPoint MoveCap = bDriveReverse ? RevTop : Cruise;
		FFixedPoint TargetSpeedMag = MoveCap * ThrottleScale;
		// Avoidance speed-yield — give way by braking, not only turning.
		TargetSpeedMag = TargetSpeedMag * SpeedYield;
		if (MaxArrivalSpeed < TargetSpeedMag) TargetSpeedMag = MaxArrivalSpeed;
		TargetSpeed = bDriveReverse ? -TargetSpeedMag : TargetSpeedMag;
	}

#if UE_ENABLE_DEBUG_DRAWING
	if (bDebugSteerTargetValid)
	{
		if (UWorld* DebugWorld = Ctx.World ? Ctx.World->GetWorld() : nullptr)
		{
			if (UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld(DebugWorld))
			{
				const float DrawLifetime = 0.05f;
				const FVector Origin(AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(), AgentPos.Z.ToFloat() + 50.0f);
				if (UE::SeinARTSMovement::DebugDraw::ShouldDrawAndReserve(DebugWorld, Origin))
				{
					const FVector TargetPos(DebugSteerTarget.X.ToFloat(), DebugSteerTarget.Y.ToFloat(), DebugSteerTarget.Z.ToFloat() + 50.0f);
					DrawDebugPoint(DebugWorld, TargetPos, 8.0f, FColor::Green, false, DrawLifetime);
					DrawDebugLine(DebugWorld, Origin, TargetPos, FColor::Green, false, DrawLifetime, 0, 2.0f);
				}
			}
		}
	}
#endif

	// -------------------------------------------------------------------
	// 6. Orbit backstop — gated like the stuck detector; a pivot-capable
	//    chassis orbits less than a wheeled one, but avoidance bending can
	//    still circle a blocked goal in legacy pursuit.
	// -------------------------------------------------------------------
	YawAccumSinceProgress += AppliedTurnAbs;
	if (bRecoveryAllowed && !bRecovering && YawAccumSinceProgress > OrbitYawLimit)
	{
		const bool bEffectiveCanReverse = Tracked.bCanReverse || MovementData.bCanReverse;
		const FFixedVector Fwd(SeinMath::Cos(CurrentYaw), SeinMath::Sin(CurrentYaw), FFixedPoint::Zero);
		const FFixedPoint ProbeDist = CachedCollisionRadius + FFixedPoint::FromInt(80);
		const FFixedVector Behind(AgentPos.X - Fwd.X * ProbeDist, AgentPos.Y - Fwd.Y * ProbeDist, AgentPos.Z);
		const FFixedVector Ahead(AgentPos.X + Fwd.X * ProbeDist, AgentPos.Y + Fwd.Y * ProbeDist, AgentPos.Z);
		if (bEffectiveCanReverse && IsFootprintPassable(Behind, Nav)) { RecoveryDir = -1; RecoveryTime = RecoverySeconds; }
		else if (IsFootprintPassable(Ahead, Nav))                     { RecoveryDir = +1; RecoveryTime = RecoverySeconds; }
		YawAccumSinceProgress = FFixedPoint::Zero;
		StuckTime = FFixedPoint::Zero;
	}

	// -------------------------------------------------------------------
	// 7. Speed integration + translate along post-turn forward + floors.
	// -------------------------------------------------------------------
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		Tracked.Acceleration, Tracked.Deceleration, DeltaTime);

	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	NewPos = ResolveNavCollision(AgentPos, NewPos, Nav,
		Ctx.bAuthoritativeDestination ? &FinalWp : nullptr);
	ApplyGroundSnapAndAltitude(NewPos, Ctx.MovementData, Nav, DeltaTime);

	{
		// Rate-limited pitch/roll smoothing — see infantry/wheeled for rationale.
		const FFixedPoint TargetPitch = ComputeSlopePitch(NewPos, NewYaw, Nav);
		const FFixedPoint TargetRoll  = ComputeSlopeRoll(NewPos, NewYaw, Nav);
		const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3); // 60°/sec
		MovementData.SmoothedPitch = SmoothAngleToward(MovementData.SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
		MovementData.SmoothedRoll  = SmoothAngleToward(MovementData.SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
		Entity.Transform.Rotation = YawPitchRoll(NewYaw, MovementData.SmoothedPitch, MovementData.SmoothedRoll);
	}

	UE_LOG(LogSeinTracked, Verbose,
		TEXT("Tracked: pos=(%.1f,%.1f) yaw=%.3f->%.3f mode=%s speed=%.2f->%.2f arrCap=%.1f rev=%d seg=%d/%d"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(),
		CurrentYaw.ToFloat(), NewYaw.ToFloat(),
		bRecovering ? TEXT("RECOVER") : (bManeuverMode ? TEXT("MANEUVER") : ((AbsCurrentSpeed > Tracked.PivotSpeed) ? TEXT("ARC") : TEXT("PIVOT"))),
		(EntryDot >= FFixedPoint::Zero ? EntryMag : -EntryMag).ToFloat(), CurrentSpeed.ToFloat(),
		MaxArrivalSpeed.ToFloat(), bDriveReverse ? 1 : 0, SegCursor, TailStartSeg);

	Entity.Transform.SetLocation(NewPos);
	// Persist velocity aligned with post-rotation forward. COMMANDED by
	// design — keeps vehicles outside the hold-escape ladder; the mode's own
	// stuck recovery above fills that role.
	MovementData.Velocity = FFixedVector(CosY * CurrentSpeed, SinY * CurrentSpeed, FFixedPoint::Zero);

	return false;
}
