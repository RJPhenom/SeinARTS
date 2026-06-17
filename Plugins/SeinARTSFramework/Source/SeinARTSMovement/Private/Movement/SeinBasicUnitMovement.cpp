/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicUnitMovement.cpp
 */

#include "Movement/SeinBasicUnitMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Components/SeinMovementComponent.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinBasicUnit, Log, All);

bool USeinBasicUnitMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadiusSq = Ctx.AcceptanceRadiusSq;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	const int32 N = Path.Waypoints.Num();
	if (N == 0) return true;

	const FFixedVector InitialPos = Entity.Transform.GetLocation();

	// ----------------------------------------------------------------------
	// BAR-idiom speed model (CP0 — see planning/MicroPlan_CP0.md). Replaces the
	// old flat `TopSpeed·dt` step with a scalar speed that ACCELERATES toward
	// TopSpeed and DECELERATES (kinematic brake) into the final waypoint, so the
	// unit visibly ramps up and slows to a stop instead of teleport-starting and
	// dead-stopping. This is the BAR ground model — NOT Infantry's CoH-style
	// alignment-gated speed (Decisions D8: framework basics = BAR; Movement+ =
	// CoH). BasicUnit keeps its translate-toward-target + face-velocity character.
	// ----------------------------------------------------------------------

	// Recover the current scalar speed from the persisted velocity vector.
	// BasicUnit is forward-only (no reverse), so the magnitude IS the scalar
	// speed. Living on the component, Velocity carries momentum across ticks AND
	// across new move orders (a re-issued MoveTo while in motion keeps speed).
	const FFixedPoint EntrySpeed = MovementData.Velocity.Size();
	FFixedPoint CurrentSpeed = EntrySpeed;

	// Kinematic arrival cap: the fastest we can still brake to rest EXACTLY at the
	// final waypoint given Deceleration (solves v² = 2·a·d). It only drops below
	// TopSpeed once inside the brake zone, so the unit cruises at TopSpeed then
	// visibly decelerates on approach rather than running flat-out into a hard stop.
	FFixedVector ToFinal = Path.Waypoints[N - 1] - InitialPos;
	ToFinal.Z = FFixedPoint::Zero;
	const FFixedPoint DistToFinal = ToFinal.Size();
	// Brake to a stop at the ACCEPTANCE RING, not the exact point. Arrival fires
	// when the unit's center enters the ring, so braking to the center leaves it
	// still doing sqrt(2*decel*Acceptance) at the ring — a hard stop. Braking to
	// the ring edge makes the unit decelerate to ~0 right as it arrives, for a
	// smooth come-to-rest. Same stopping position either way; only the arrival
	// speed differs. (BrakeDist clamps to 0 once already inside the ring.)
	const FFixedPoint Acceptance = SeinMath::Sqrt(AcceptanceRadiusSq);
	const FFixedPoint BrakeDist = (DistToFinal > Acceptance) ? (DistToFinal - Acceptance) : FFixedPoint::Zero;
	const FFixedPoint ArrivalCap = KinematicArrivalSpeedCap(BrakeDist, MovementData.Deceleration);
	// Cruise target = terrain-scaled top speed (mud slows, road speeds); the accel/decel
	// ramp + Velocity below then reflect the reduced speed honestly. The OneStep arrival
	// reference further down stays at raw TopSpeed (stable max-step, never miss a waypoint).
	FFixedPoint TargetSpeed = EffectiveTopSpeed(Ctx);
	if (ArrivalCap < TargetSpeed) TargetSpeed = ArrivalCap;

	// Smoothstep the scalar speed toward the target — Acceleration when growing,
	// Deceleration when shrinking. This is the entire CP0 feel change.
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		MovementData.Acceleration, MovementData.Deceleration, DeltaTime);

	// Per-tick travel budget is now the RAMPED speed, not flat TopSpeed.
	FFixedVector Pos = InitialPos;
	FFixedPoint RemainingStep = CurrentSpeed * DeltaTime;
	// Local avoidance applies to this tick's FIRST movement step only.
	bool bAvoidanceApplied = false;

	// Reference full-speed step for the intermediate-waypoint arrival radius. Kept
	// at TopSpeed·dt (a stable max-step reference) so a low-speed ramp tick doesn't
	// fail to consume a waypoint it has effectively reached.
	const FFixedPoint OneStep = MovementData.TopSpeed * DeltaTime;

	while (RemainingStep > FFixedPoint::Zero && CurrentWaypointIndex < N)
	{
		const FFixedVector Target = Path.Waypoints[CurrentWaypointIndex];
		FFixedVector Delta = Target - Pos;
		Delta.Z = FFixedPoint::Zero;
		const FFixedPoint DistSq = Delta.SizeSquared();

		const bool bIsFinalWaypoint = (CurrentWaypointIndex == N - 1);
		const FFixedPoint ArriveRadiusSq = bIsFinalWaypoint ? AcceptanceRadiusSq : OneStep * OneStep;

		if (DistSq <= ArriveRadiusSq)
		{
			if (bIsFinalWaypoint)
			{
				// Arrived within the acceptance ring — STOP IN PLACE. Do NOT snap
				// Pos to the exact destination: that snap teleported the last
				// AcceptanceRadius, which reads as a jarring jump-into-rest now that
				// the unit decelerates into the goal. Stopping anywhere inside the
				// acceptance envelope is the contract (matches USeinInfantryMovement).
				++CurrentWaypointIndex;
				break;
			}
			// Intermediate waypoint: snap-and-advance (small, on-path) then keep stepping.
			Pos.X = Target.X;
			Pos.Y = Target.Y;
			++CurrentWaypointIndex;
			continue;
		}

		const FFixedPoint Dist = Delta.Size();
		const FFixedPoint StepLen = (Dist < RemainingStep) ? Dist : RemainingStep;

		FFixedVector Dir = FFixedVector::GetSafeNormal(Delta);
		// Local avoidance — bend only this tick's first step (the primary steering
		// direction); later sub-steps consuming close waypoints use true geometry.
		// Soft layer; the penetration floor still guarantees no overlap.
		if (!bAvoidanceApplied) { Dir = ApplyAvoidanceSteer(Ctx, Dir); bAvoidanceApplied = true; }

		Pos.X = Pos.X + Dir.X * StepLen;
		Pos.Y = Pos.Y + Dir.Y * StepLen;

		RemainingStep = RemainingStep - StepLen;
	}

	Pos = ResolveNavCollision(InitialPos, Pos, Nav);

	ApplyGroundSnapAndAltitude(Pos, Ctx.MovementData, Nav, DeltaTime);

	Entity.Transform.SetLocation(Pos);

	const bool bArrived = (CurrentWaypointIndex >= N);

	// Smooth turn-to-velocity from the actual moved delta. Velocity-based (rather
	// than waypoint-direction-based) means a blocked tick naturally holds facing.
	FFixedVector MoveDelta = Pos - InitialPos;
	MoveDelta.Z = FFixedPoint::Zero;
	FFixedVector MoveDir = FFixedVector::ZeroVector;
	if (MoveDelta.SizeSquared() > FFixedPoint::Epsilon)
	{
		MoveDir = FFixedVector::GetSafeNormal(MoveDelta);
		const FFixedPoint DesiredYaw = SeinMath::Atan2(MoveDir.Y, MoveDir.X);
		const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
		const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		const FFixedPoint MaxTurn = MovementData.TurnRate * DeltaTime;
		const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
		const FFixedPoint FinalYaw = CurrentYaw + AppliedTurn;
		// Rate-limited pitch/roll smoothing (60°/sec) — see SeinInfantryMovement.
		const FFixedPoint TargetPitch = ComputeSlopePitch(Pos, FinalYaw, Nav);
		const FFixedPoint TargetRoll  = ComputeSlopeRoll(Pos, FinalYaw, Nav);
		const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3);
		MovementData.SmoothedPitch = SmoothAngleToward(MovementData.SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
		MovementData.SmoothedRoll  = SmoothAngleToward(MovementData.SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
		Entity.Transform.Rotation = YawPitchRoll(FinalYaw, MovementData.SmoothedPitch, MovementData.SmoothedRoll);
	}

	// Persist world-frame velocity at the RAMPED scalar speed so momentum carries
	// across ticks and orders. On arrival, come to rest (zero) so the unit settles
	// cleanly at the destination. When blocked (no net move) keep the scalar on the
	// current facing so a stalled unit retains its intended heading and retries —
	// ResolveNavCollision usually axis-slides it free within a tick or two.
	if (bArrived)
	{
		MovementData.Velocity = FFixedVector::ZeroVector;
	}
	else if (MoveDir.SizeSquared() > FFixedPoint::Epsilon)
	{
		MovementData.Velocity = FFixedVector(MoveDir.X * CurrentSpeed, MoveDir.Y * CurrentSpeed, FFixedPoint::Zero);
	}
	else
	{
		const FFixedVector Forward = Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
		MovementData.Velocity = FFixedVector(Forward.X * CurrentSpeed, Forward.Y * CurrentSpeed, FFixedPoint::Zero);
	}

	UE_LOG(LogSeinBasicUnit, Verbose,
		TEXT("BasicUnit: pre=(%.2f,%.2f) post=(%.2f,%.2f) speed=%.1f->%.1f tgt=%.1f wp[%d/%d]"),
		InitialPos.X.ToFloat(), InitialPos.Y.ToFloat(),
		Pos.X.ToFloat(), Pos.Y.ToFloat(),
		EntrySpeed.ToFloat(), CurrentSpeed.ToFloat(), TargetSpeed.ToFloat(),
		CurrentWaypointIndex, N);

	return bArrived;
}
