/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWheeledVehicleMovement.cpp
 * @brief   Bicycle-kinematics pure-pursuit controller.
 */

#include "Movement/SeinWheeledVehicleMovement.h"
#include "SeinARTSMovementModule.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
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

USeinWheeledVehicleMovement::USeinWheeledVehicleMovement() = default;

UScriptStruct* USeinWheeledVehicleMovement::GetMovementDataStruct() const
{
	return FSeinWheeledMovementData::StaticStruct();
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

void USeinWheeledVehicleMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;

	// Wheels self-center per move action. Velocity intentionally preserved
	// so a vehicle reordered mid-drive doesn't instant-stop.
	CurrentSteer = FFixedPoint::Zero;

	// Auto-reverse latch -- if the destination is behind the unit and close
	// enough (per MovementData reverse tunables), commit to driving backward.
	const int32 N = Path.Waypoints.Num();
	bIsReversing = (N > 0) && ShouldAutoReverse(
		Entity.Transform.GetLocation(),
		Entity.Transform.Rotation,
		Path.Waypoints[N - 1],
		MovementData);
}

bool USeinWheeledVehicleMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadiusSq = Ctx.AcceptanceRadiusSq;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	// Unwrap wheeled-specific sub-data once. Every wheeled tunable —
	// Wheelbase, MaxSteerAngle, SteerResponse, LookAhead*, ArrivalSlowdown,
	// TurnSpeedFloor, SharpTurnBrake* — lives here. Defaults from
	// FSeinWheeledMovementData's in-class initializers stand in when
	// MovementClassData is unauthored.
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

	const FFixedVector AgentPos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// Drive direction: auto-reverse latch (one-shot at OnMoveBegin for
	// behind-the-chassis goals). All path segments are forward-only.
	const bool bDriveReverse = bIsReversing;
	const bool bYawTargetFlipped = bDriveReverse;

	// -------------------------------------------------------------------
	// 2. Arrival check (within / overshoot). Return true on hit so the
	//    action ends.
	// -------------------------------------------------------------------
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const bool bWithinAcceptance = ToFinal.SizeSquared() <= AcceptanceRadiusSq;
		const FFixedPoint VicinityRadiusSq = AcceptanceRadiusSq * FFixedPoint::FromInt(4);
		const FFixedPoint OvershootSpeedCap = MovementData.TopSpeed / FFixedPoint::FromInt(3);
		const bool bOvershoot = IsOvershootArrival(
			AgentPos, FinalWp, Entity.Transform.Rotation,
			CurrentSpeed, VicinityRadiusSq, OvershootSpeedCap);
		if (bWithinAcceptance || bOvershoot)
		{
			MovementData.Velocity = FFixedVector::ZeroVector;
			CurrentSteer = FFixedPoint::Zero;
			return true;
		}
	}

	// -------------------------------------------------------------------
	// 3. Cross-over waypoint advance -- see USeinMovement::
	//    AdvanceWaypointAlongPath. Uses dot-product crossover (robust
	//    to overshoot at speed) plus a distance fallback.
	// -------------------------------------------------------------------
	{
		const FFixedPoint OneStep = MovementData.TopSpeed * DeltaTime;
		const FFixedPoint CloseRadius = (OneStep * FFixedPoint::Two > FFixedPoint::FromInt(50))
			? OneStep * FFixedPoint::Two : FFixedPoint::FromInt(50);
		AdvanceWaypointAlongPath(CurrentWaypointIndex, Path, AgentPos, CloseRadius);
	}

	// -------------------------------------------------------------------
	// 4. Carrot on the polyline. Speed-adaptive distance so the carrot
	//    sits further ahead at speed for smoother corner arcs.
	// -------------------------------------------------------------------
	const FFixedPoint LookAheadFloor = (Wheeled.LookAheadDistance > FFixedPoint::Zero)
		? Wheeled.LookAheadDistance : FFixedPoint::FromInt(100);
	const FFixedPoint AbsCurrentSpeed = (CurrentSpeed < FFixedPoint::Zero)
		? -CurrentSpeed : CurrentSpeed;
	const FFixedPoint LookAhead = ComputeAdaptiveLookAhead(
		LookAheadFloor, Wheeled.LookAheadTimeHorizon, AbsCurrentSpeed);
	// `MaxCornerAngleRadians` was a deprecated carrot-weighting input — pass zero.
	// `ResolveLookAheadPoint` now does cluster-skip thinning internally;
	// no per-call carrot-corner-weighting input is needed.
	const FFixedVector LookAheadPoint = ResolveLookAheadPoint(
		AgentPos, Path, CurrentWaypointIndex, LookAhead,
		FFixedPoint::Zero);

	FFixedVector ToTarget = LookAheadPoint - AgentPos;
	ToTarget.Z = FFixedPoint::Zero;

	// Local avoidance — bend the carrot direction around nearby units in the FORWARD
	// frame, BEFORE the auto-reverse yaw-flip below (step 5), so the dodge isn't
	// inverted when backing up. Normalized in/out: only the carrot ANGLE is consumed
	// downstream (Atan2), its magnitude is unused. Soft layer; the penetration floor
	// still guarantees no overlap.
	if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
	{
		ToTarget = ApplyAvoidanceSteer(Ctx, FFixedVector::GetSafeNormal(ToTarget));
	}

#if UE_ENABLE_DEBUG_DRAWING
	// Carrot debug viz. Gated on Sein.Nav.Show.SteeringVectors. Green dot =
	// carrot point, green line = agent -> carrot.
	if (UWorld* DebugWorld = Ctx.World ? Ctx.World->GetWorld() : nullptr)
	{
		if (UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld(DebugWorld))
		{
			const float DrawLifetime = static_cast<float>(Ctx.DeltaTime.ToFloat()) + 0.01f;
			const FVector Origin(AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(), AgentPos.Z.ToFloat() + 50.0f);
			// Use the carrot's interpolated Z (from ResolveLookAheadPoint) rather
			// than forcing AgentPos.Z — on slopes the carrot sits along the path's
			// elevation profile, so the debug dot + steering line follow the path's
			// pitch instead of a flat Z plane at the chassis.
			const FVector CarrotPos(LookAheadPoint.X.ToFloat(), LookAheadPoint.Y.ToFloat(), LookAheadPoint.Z.ToFloat() + 50.0f);
			DrawDebugPoint(DebugWorld, CarrotPos, 8.0f, FColor::Green, false, DrawLifetime);
			DrawDebugLine(DebugWorld, Origin, CarrotPos, FColor::Green, false, DrawLifetime, 0, 2.0f);
		}
	}
#endif

	// -------------------------------------------------------------------
	// 5. Pure path-pull steer -- yaw error toward the carrot.
	//
	//    bYawTargetFlipped (auto-reverse) flips the desired yaw to
	//    back-faces-goal. bDriveReverse inverts the final steer for
	//    bicycle reverse kinematics (a steered front wheel arcs the
	//    rear opposite to the forward case when the chassis is moving
	//    backward).
	// -------------------------------------------------------------------
	FFixedPoint DesiredSteer = FFixedPoint::Zero;
	// Hoisted out of the `if` block so the sharp-turn brake (step 8.5)
	// can read it. Zero when ToTarget is null — no brake fires.
	FFixedPoint AbsYawErr = FFixedPoint::Zero;
	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
	if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = bYawTargetFlipped
			? SeinMath::Atan2(-ToTarget.Y, -ToTarget.X)
			: SeinMath::Atan2(ToTarget.Y, ToTarget.X);
		const FFixedPoint YawErr = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		AbsYawErr = (YawErr < FFixedPoint::Zero) ? -YawErr : YawErr;
		FFixedPoint PathPullSteer = ClampFP(YawErr, -Wheeled.MaxSteerAngle, Wheeled.MaxSteerAngle);
		if (bDriveReverse) PathPullSteer = -PathPullSteer;
		DesiredSteer = PathPullSteer;
	}

	// -------------------------------------------------------------------
	// 6. Smooth CurrentSteer toward DesiredSteer (exponential approach).
	// -------------------------------------------------------------------
	{
		FFixedPoint Alpha = Wheeled.SteerResponse * DeltaTime;
		if (Alpha < FFixedPoint::Zero) Alpha = FFixedPoint::Zero;
		if (Alpha > FFixedPoint::One)  Alpha = FFixedPoint::One;
		CurrentSteer = CurrentSteer + (DesiredSteer - CurrentSteer) * Alpha;
	}

	// -------------------------------------------------------------------
	// 7. Bicycle yaw rate: w = (v / L) * tan(d). Speed-dependent -- a
	//    stationary vehicle cannot pivot. Outer cap by MovementData.TurnRate
	//    is a safety lid against extreme combos.
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
	const FFixedPoint NewYaw = CurrentYaw + YawStep;
	// Pitch is applied after ground snap (step 12) — use a placeholder here,
	// overwritten below once NewPos is resolved.

	// Steering diagnostic -- log every tick at Verbose.
	UE_LOG(LogSeinWheeled, Verbose,
		TEXT("Wheeled: pos=(%.1f,%.1f) yaw=%.3f currSteer=%.3f desiredSteer=%.3f speed=%.1f"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(),
		CurrentYaw.ToFloat(), CurrentSteer.ToFloat(), DesiredSteer.ToFloat(),
		CurrentSpeed.ToFloat());

	// -------------------------------------------------------------------
	// 8. Throttle scaling -- quadratic falloff with |steer| / MaxSteer.
	//    At full steer, throttle = TurnSpeedFloor; at zero steer, full
	//    throttle.
	// -------------------------------------------------------------------
	FFixedPoint TurnScale = FFixedPoint::One;
	if (Wheeled.TurnSpeedFloor < FFixedPoint::One && Wheeled.MaxSteerAngle > FFixedPoint::Epsilon)
	{
		const FFixedPoint AbsSteer = (CurrentSteer < FFixedPoint::Zero) ? -CurrentSteer : CurrentSteer;
		FFixedPoint T = AbsSteer / Wheeled.MaxSteerAngle;
		if (T > FFixedPoint::One) T = FFixedPoint::One;
		const FFixedPoint TSq = T * T;
		TurnScale = FFixedPoint::One - (FFixedPoint::One - Wheeled.TurnSpeedFloor) * TSq;
	}

	// -------------------------------------------------------------------
	// 8.5. Sharp-turn brake — react to the COMMANDED turn (raw |YawErr|)
	//      rather than the smoothed CurrentSteer. The TurnSpeedFloor brake
	//      (step 8) lags by ~SteerResponse time constant; in that window a
	//      vehicle commanded to turn 180° at full speed keeps accelerating
	//      until the smoothed steer catches up, undershooting the path arc.
	//      This brake engages immediately on a sharp commanded turn so the
	//      chassis decelerates while the steer is settling.
	//
	//      Scales linearly with both yaw-error magnitude (above the
	//      threshold angle) and current-speed / TopSpeed — slow vehicles
	//      pivot slowly anyway via bicycle kinematics (yaw rate = v/L * tan(steer)),
	//      no need to brake further. At low speed the velocity factor pulls
	//      the brake toward 1.0 regardless of how sharp the turn is.
	// -------------------------------------------------------------------
	FFixedPoint SharpTurnScale = FFixedPoint::One;
	if (Wheeled.SharpTurnBrakeStrength > FFixedPoint::Zero
		&& Wheeled.SharpTurnBrakeAngle < FFixedPoint::Pi
		&& AbsYawErr > Wheeled.SharpTurnBrakeAngle)
	{
		const FFixedPoint SharpRange = FFixedPoint::Pi - Wheeled.SharpTurnBrakeAngle;
		if (SharpRange > FFixedPoint::Epsilon)
		{
			// Angle factor: 0 just above threshold → 1 at π (180° turn).
			FFixedPoint AngleT = (AbsYawErr - Wheeled.SharpTurnBrakeAngle) / SharpRange;
			if (AngleT > FFixedPoint::One) AngleT = FFixedPoint::One;

			// Velocity factor: 0 at rest → 1 at TopSpeed. Suppresses the
			// brake at low speed where it'd just slow normal start-up.
			FFixedPoint SpeedT = FFixedPoint::One;
			if (MovementData.TopSpeed > FFixedPoint::Epsilon)
			{
				const FFixedPoint AbsCurr = (CurrentSpeed < FFixedPoint::Zero) ? -CurrentSpeed : CurrentSpeed;
				SpeedT = AbsCurr / MovementData.TopSpeed;
				if (SpeedT > FFixedPoint::One)  SpeedT = FFixedPoint::One;
				if (SpeedT < FFixedPoint::Zero) SpeedT = FFixedPoint::Zero;
			}

			// New semantics: SharpTurnBrakeStrength is 0..1 where 0 = no brake
			// and 1 = full stop at max sharp turn at TopSpeed. Reduction is
			// linear in all three factors: Strength × AngleT × SpeedT.
			const FFixedPoint Reduction = Wheeled.SharpTurnBrakeStrength * AngleT * SpeedT;
			SharpTurnScale = FFixedPoint::One - Reduction;
			if (SharpTurnScale < FFixedPoint::Zero) SharpTurnScale = FFixedPoint::Zero;
		}
	}

	// -------------------------------------------------------------------
	// 9. Kinematic arrival cap (v^2 = 2*a*d) + optional linear floor
	//    inside ArrivalSlowdownDistance.
	//
	// The cap aims to bring the chassis to zero speed at the EDGE of the
	// acceptance ring, not at the goal point itself. Without this offset,
	// the arrival check (step 2) hard-zeros velocity the moment the
	// chassis crosses into AcceptanceRadius.
	// -------------------------------------------------------------------
	FFixedPoint MaxArrivalSpeed;
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinal = ToFinal.Size();
		// AcceptanceRadius moved to FSeinNavigationComponent in the Phase-5
		// decomposition — read it from NavData, falling back to zero (no
		// safety brake offset) when NavData is null.
		const FFixedPoint Acceptance = Ctx.NavData ? Ctx.NavData->AcceptanceRadius : FFixedPoint::Zero;
		const FFixedPoint BrakeDist = (DistFinal > Acceptance)
			? (DistFinal - Acceptance)
			: FFixedPoint::Zero;
		MaxArrivalSpeed = KinematicArrivalSpeedCap(BrakeDist, MovementData.Deceleration);
		if (Wheeled.ArrivalSlowdownDistance > FFixedPoint::Zero && DistFinal < Wheeled.ArrivalSlowdownDistance)
		{
			const FFixedPoint LinearCap = MovementData.TopSpeed * (DistFinal / Wheeled.ArrivalSlowdownDistance);
			if (LinearCap < MaxArrivalSpeed) MaxArrivalSpeed = LinearCap;
		}
	}

	// -------------------------------------------------------------------
	// 10. Target speed magnitude -- forward uses MoveSpeed, reverse uses
	//     ReverseMaxSpeed (or MoveSpeed/2 fallback). Apply throttle scale
	//     + arrival cap, then sign-restore for reverse.
	// -------------------------------------------------------------------
	FFixedPoint TargetSpeedMag = bDriveReverse
		? ((MovementData.ReverseTopSpeed > FFixedPoint::Zero) ? MovementData.ReverseTopSpeed : MovementData.TopSpeed * FFixedPoint::Half)
		: MovementData.TopSpeed;
	// Compose both turn brakes — TurnSpeedFloor (smoothed-steer-based) and
	// SharpTurnScale (commanded-yaw-based). They fire for related-but-distinct
	// reasons; multiplying them lets the more aggressive brake dominate while
	// keeping each one's behavior unchanged when the other doesn't fire.
	TargetSpeedMag = TargetSpeedMag * TurnScale * SharpTurnScale;
	if (MaxArrivalSpeed < TargetSpeedMag) TargetSpeedMag = MaxArrivalSpeed;
	const FFixedPoint TargetSpeed = bDriveReverse ? -TargetSpeedMag : TargetSpeedMag;

	UE_LOG(LogSeinWheeled, Verbose,
		TEXT("WheeledBrake: baseSpeed=%.1f targetMag=%.1f currSpeed=%.1f reverse=%d turnScale=%.3f sharpScale=%.3f absYawErr=%.3f arrivalCap=%.1f"),
		MovementData.TopSpeed.ToFloat(),
		TargetSpeedMag.ToFloat(),
		CurrentSpeed.ToFloat(),
		bDriveReverse ? 1 : 0,
		TurnScale.ToFloat(),
		SharpTurnScale.ToFloat(),
		AbsYawErr.ToFloat(),
		MaxArrivalSpeed.ToFloat());

	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		MovementData.Acceleration, MovementData.Deceleration, DeltaTime);

	// -------------------------------------------------------------------
	// 11. Translate along the post-rotation forward.
	// -------------------------------------------------------------------
	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	// -------------------------------------------------------------------
	// 12. Footprint-aware nav collision floor + ground snap.
	// -------------------------------------------------------------------
	NewPos = ResolveNavCollision(AgentPos, NewPos, Nav);
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
	MovementData.Velocity = FFixedVector(CosY * CurrentSpeed, SinY * CurrentSpeed, FFixedPoint::Zero);

	return false;
}
