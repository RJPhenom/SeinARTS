/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInfantryMovement.cpp
 */

#include "Movement/SeinInfantryMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinMovement, Log, All);

void USeinInfantryMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// MovementData.Velocity is intentionally preserved — momentum carries
	// across order changes. With high Acceleration / Deceleration the
	// transition from one velocity to another is near-instant but never a
	// hard zero-snap.
}

bool USeinInfantryMovement::Tick(const FSeinMovementContext& Ctx)
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

	// Recover signed scalar speed from persisted velocity vector. See
	// SeinWheeledVehicleMovement::Tick for the precision rationale.
	const FFixedQuaternion EntryRot = Entity.Transform.Rotation;
	const FFixedVector EntryForward = EntryRot.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint EntryDot = MovementData.Velocity.X * EntryForward.X + MovementData.Velocity.Y * EntryForward.Y;
	const FFixedPoint EntryMag = MovementData.Velocity.Size();
	FFixedPoint CurrentSpeed = (EntryDot >= FFixedPoint::Zero) ? EntryMag : -EntryMag;

	const FFixedVector PrePos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// Arrival — within AcceptanceRadius OR overshoot (close + slow + heading
	// away). No position snap; stopping inside the acceptance envelope is
	// the contract.
	{
		FFixedVector ToFinal = FinalWp - PrePos;
		ToFinal.Z = FFixedPoint::Zero;
		const bool bWithinAcceptance = ToFinal.SizeSquared() <= AcceptanceRadiusSq;

		const FFixedPoint VicinityRadiusSq = AcceptanceRadiusSq * FFixedPoint::FromInt(4);
		const FFixedPoint OvershootSpeedCap = MovementData.TopSpeed / FFixedPoint::FromInt(3);
		const bool bOvershoot = IsOvershootArrival(
			PrePos, FinalWp, Entity.Transform.Rotation,
			CurrentSpeed, VicinityRadiusSq, OvershootSpeedCap);

		if (bWithinAcceptance || bOvershoot)
		{
			MovementData.Velocity = FFixedVector::ZeroVector;
			return true;
		}
	}

	// Advance through waypoints we've effectively passed. Tighter radius
	// than vehicles — infantry navigates more precisely.
	const FFixedPoint OneStep = MovementData.TopSpeed * DeltaTime;
	const FFixedPoint AdvanceRadiusSq = OneStep * OneStep;
	while (CurrentWaypointIndex < N - 1)
	{
		FFixedVector ToWp = Path.Waypoints[CurrentWaypointIndex] - PrePos;
		ToWp.Z = FFixedPoint::Zero;
		if (ToWp.SizeSquared() <= AdvanceRadiusSq) ++CurrentWaypointIndex;
		else break;
	}

	// Steer toward the current waypoint directly — infantry doesn't need
	// look-ahead carrots; pivot is fast enough that immediate-waypoint
	// targeting produces clean motion.
	const FFixedVector TargetWp = Path.Waypoints[CurrentWaypointIndex];
	FFixedVector ToTarget = TargetWp - PrePos;
	ToTarget.Z = FFixedPoint::Zero;
	const FFixedPoint DistToTargetSq = ToTarget.SizeSquared();

	// Compute the unit vector toward the target. If the target is on top
	// of the unit (zero-length), hold previous facing and brake to zero.
	FFixedVector Dir = FFixedVector::ZeroVector;
	if (DistToTargetSq > FFixedPoint::Epsilon)
	{
		Dir = FFixedVector::GetSafeNormal(ToTarget);
		// Local avoidance — bend the desired direction around nearby units (steer
		// precomputed one-sided at PreTick by FSeinAvoidanceSystem). Soft layer; the
		// penetration floor still guarantees no overlap. Arrival + waypoint advance use
		// the true geometry (DistToTargetSq / ToTarget), not Dir, so they're unaffected.
		Dir = ApplyAvoidanceSteer(Ctx, Dir);
	}

	// Alignment-scaled target speed. The unit walks along its CURRENT
	// FACING (Forward), not directly toward Dir — and TargetSpeed scales
	// by how aligned its facing is with the desired direction. Effect:
	//   - facing matches Dir → full MoveSpeed
	//   - facing 90° off Dir → zero target speed (unit decels to stop)
	//   - facing 180° off Dir → zero target speed (decel; rotation
	//     simultaneously chases Dir, eventually realigning)
	// This is what produces the natural "decel-rotate-accel" arc when an
	// infantry unit gets a sharp direction-change order, instead of the
	// translate-instantly-while-facing-trails sliding bug. Non-strafing
	// by construction (facing always tracks movement direction); strafe-
	// capable units would need a separate mode that decouples Dir/Forward.
	const FFixedVector ForwardThisTick = EntryRot.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint AlignmentDot = (Dir.SizeSquared() > FFixedPoint::Epsilon)
		? FFixedVector::DotProduct(Dir, ForwardThisTick)
		: FFixedPoint::Zero;
	const FFixedPoint Alignment = (AlignmentDot > FFixedPoint::Zero) ? AlignmentDot : FFixedPoint::Zero;

	// Smoothstep speed toward (MoveSpeed × Alignment), capped by kinematic-
	// arrival braking against the FINAL waypoint so the unit visibly slows
	// on approach instead of running flat-out into a hard stop. With high
	// Accel/Decel (recommended for snappy infantry) the brake zone is small
	// and the curve remains continuous.
	FFixedPoint TargetSpeed = (DistToTargetSq > FFixedPoint::Epsilon)
		? (MovementData.TopSpeed * Alignment) : FFixedPoint::Zero;
	{
		FFixedVector ToFinal = FinalWp - PrePos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinal = ToFinal.Size();
		const FFixedPoint MaxArrivalSpeed = KinematicArrivalSpeedCap(DistFinal, MovementData.Deceleration);
		if (MaxArrivalSpeed < TargetSpeed) TargetSpeed = MaxArrivalSpeed;
	}
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		MovementData.Acceleration, MovementData.Deceleration, DeltaTime);

	// Translate along CURRENT FACING (Forward), not Dir. Don't overshoot
	// the target waypoint within one tick — clamp step to planar distance
	// so we land exactly there if we'd pass it. (Overshoot clamp is
	// approximate now since we're moving along Forward, not directly toward
	// the waypoint; uses straight-line distance as a conservative bound.)
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = PrePos;
	if (StepLen > FFixedPoint::Epsilon)
	{
		const FFixedPoint Dist = SeinMath::Sqrt(DistToTargetSq);
		const FFixedPoint ClampedStep = (StepLen < Dist) ? StepLen : Dist;
		NewPos.X = NewPos.X + ForwardThisTick.X * ClampedStep;
		NewPos.Y = NewPos.Y + ForwardThisTick.Y * ClampedStep;
	}

	// Hard nav-collision resolve before Z-snap so axis-slide chooses the right
	// XY first, then Z reflects the actual cell we end up at. When the move's final
	// waypoint is an authoritative destination (cover slot), pass it so the unit can
	// step onto it even though its cell is bake-blocked — the slot overrules the
	// coarse bake (root CLAUDE.md #6).
	const FFixedVector AuthDest = (N > 0) ? Path.Waypoints[N - 1] : FFixedVector::ZeroVector;
	NewPos = ResolveNavCollision(PrePos, NewPos, Nav,
		Ctx.bAuthoritativeDestination ? &AuthDest : nullptr);

	// Z-snap to nav ground + Altitude offset (default 0 = ground; non-zero =
	// jump/vault arc).
	ApplyGroundSnapAndAltitude(NewPos, Ctx.MovementData, Nav, DeltaTime);

	Entity.Transform.SetLocation(NewPos);

	// Smooth turn-to-velocity. Rotates Forward toward Dir at TurnRate even
	// when the unit is stationary — that's the whole point of the alignment-
	// scaled speed: when off-direction, TargetSpeed = 0 (unit slows / stops),
	// and rotation is the only way Alignment recovers. Without rotating in
	// place, a stopped-but-not-aligned unit would be stuck forever.
	if (Dir.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = SeinMath::Atan2(Dir.Y, Dir.X);
		const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
		const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		const FFixedPoint MaxTurn = MovementData.TurnRate * DeltaTime;
		const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
		const FFixedPoint FinalYaw = CurrentYaw + AppliedTurn;
		// Rate-limited pitch/roll smoothing. The compute functions already
		// cap the per-tick TARGET magnitude (so sustained extreme tilts
		// from walls-just-below-step-height can't persist), but a fresh
		// instant jump from 5° to 20° (cap) still reads as a "pop." Apply
		// SmoothAngleToward to ramp the visible angle at 60°/sec (π/3),
		// so brief wall-edge spikes only contribute a few degrees before
		// the spike passes. Smoothing state lives on MovementData so it
		// persists across move orders.
		const FFixedPoint TargetPitch = ComputeSlopePitch(NewPos, FinalYaw, Nav);
		const FFixedPoint TargetRoll  = ComputeSlopeRoll(NewPos, FinalYaw, Nav);
		const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3); // 60°/sec
		MovementData.SmoothedPitch = SmoothAngleToward(MovementData.SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
		MovementData.SmoothedRoll  = SmoothAngleToward(MovementData.SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
		Entity.Transform.Rotation = YawPitchRoll(FinalYaw, MovementData.SmoothedPitch, MovementData.SmoothedRoll);

		UE_LOG(LogSeinMovement, Verbose,
			TEXT("Infantry: pre=(%.2f,%.2f) post=(%.2f,%.2f) align=%.2f speed=%.2f→%.2f wp[%d/%d] yaw=%.3f→%.3f"),
			PrePos.X.ToFloat(), PrePos.Y.ToFloat(),
			NewPos.X.ToFloat(), NewPos.Y.ToFloat(),
			Alignment.ToFloat(),
			EntryMag.ToFloat() * (EntryDot >= FFixedPoint::Zero ? 1.0f : -1.0f), CurrentSpeed.ToFloat(),
			CurrentWaypointIndex, N,
			CurrentYaw.ToFloat(), (CurrentYaw + AppliedTurn).ToFloat());
	}

	// Persist velocity vector. Translation was along Forward at signed
	// scalar CurrentSpeed, so persisted Velocity now matches actual
	// translation direction (post-rotation Forward) — anim BPs reading
	// Velocity see consistent direction-vs-facing.
	const FFixedVector NewForward = Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
	MovementData.Velocity = FFixedVector(NewForward.X * CurrentSpeed, NewForward.Y * CurrentSpeed, FFixedPoint::Zero);

	return false;
}
