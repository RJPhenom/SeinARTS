/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTrackedVehicleMovement.cpp
 */

#include "Movement/SeinTrackedVehicleMovement.h"
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

UScriptStruct* USeinTrackedVehicleMovement::GetMovementDataStruct() const
{
	return FSeinTrackedMovementData::StaticStruct();
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

void USeinTrackedVehicleMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return;

	FSeinEntity& Entity = Ctx.Entity;
	const FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;

	// Preserve MovementData.Velocity so reorders carry momentum.
	const int32 N = Path.Waypoints.Num();
	bIsReversing = (N > 0) && ShouldAutoReverse(
		Entity.Transform.GetLocation(),
		Entity.Transform.Rotation,
		Path.Waypoints[N - 1],
		MovementData);
}

bool USeinTrackedVehicleMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	const FSeinPath& Path = Ctx.Path;

	// Unwrap the tracked-specific sub-data once. Defaults from the struct's
	// in-class member initializers stand in if unauthored, so entity classes
	// that pick TrackedVehicle but haven't filled in MovementClassData still
	// drive sensibly.
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

	const FFixedVector AgentPos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// -------------------------------------------------------------------
	// 2. Arrival check — within AcceptanceRadius OR overshoot. No snap.
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
			UE_LOG(LogSeinTracked, Verbose,
				TEXT("Tracked arrival: within=%d overshoot=%d distSq=%.1f speed=%.2f"),
				bWithinAcceptance ? 1 : 0, bOvershoot ? 1 : 0,
				ToFinal.SizeSquared().ToFloat(), CurrentSpeed.ToFloat());
			MovementData.Velocity = FFixedVector::ZeroVector;
			return true;
		}
	}

	// -------------------------------------------------------------------
	// 3. Advance through waypoints we've effectively passed.
	// -------------------------------------------------------------------
	const FFixedPoint OneStep = MovementData.TopSpeed * DeltaTime;
	const FFixedPoint AdvanceRadius = (OneStep * FFixedPoint::Two > FFixedPoint::FromInt(50))
		? OneStep * FFixedPoint::Two : FFixedPoint::FromInt(50);
	const FFixedPoint AdvanceRadiusSq = AdvanceRadius * AdvanceRadius;
	while (CurrentWaypointIndex < N - 1)
	{
		FFixedVector ToWp = Path.Waypoints[CurrentWaypointIndex] - AgentPos;
		ToWp.Z = FFixedPoint::Zero;
		if (ToWp.SizeSquared() <= AdvanceRadiusSq) ++CurrentWaypointIndex;
		else break;
	}

	// -------------------------------------------------------------------
	// 4. Steering carrot on the polyline. Speed-adaptive look-ahead when
	//    `LookAheadTimeHorizon > 0` (designer opt-in); floor at
	//    `LookAheadDistance` otherwise.
	// -------------------------------------------------------------------
	const FFixedPoint LookAheadFloor = (Tracked.LookAheadDistance > FFixedPoint::Zero)
		? Tracked.LookAheadDistance : FFixedPoint::FromInt(100);
	const FFixedPoint AbsCurrentSpeed = (CurrentSpeed < FFixedPoint::Zero)
		? -CurrentSpeed : CurrentSpeed;
	const FFixedPoint LookAhead = ComputeAdaptiveLookAhead(
		LookAheadFloor, Tracked.LookAheadTimeHorizon, AbsCurrentSpeed);
	const FFixedVector LookAheadPoint = ResolveLookAheadPoint(
		AgentPos, Path, CurrentWaypointIndex, LookAhead);

	FFixedVector ToTarget = LookAheadPoint - AgentPos;
	ToTarget.Z = FFixedPoint::Zero;

	// Local avoidance — bend the carrot direction around nearby units in the FORWARD
	// frame, BEFORE the reverse negate (EffectiveToTarget below), so the dodge isn't
	// inverted when backing up. Normalized in/out: only the carrot ANGLE is consumed
	// downstream, its magnitude is unused. Soft layer; the penetration floor still
	// guarantees no overlap.
	if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
	{
		ToTarget = ApplyAvoidanceSteer(Ctx, FFixedVector::GetSafeNormal(ToTarget));
	}

#if UE_ENABLE_DEBUG_DRAWING
	// Carrot point + agent-to-carrot line under the SeinSteeringVectors show flag.
	if (UWorld* DebugWorld = Ctx.World ? Ctx.World->GetWorld() : nullptr)
	{
		if (UE::SeinARTSMovement::IsSteeringShowFlagOnForWorld(DebugWorld))
		{
			const float DrawLifetime = 0.05f;
			const FVector Origin(AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(), AgentPos.Z.ToFloat() + 50.0f);
			if (UE::SeinARTSMovement::DebugDraw::ShouldDrawAndReserve(DebugWorld, Origin))
			{
				// Use the carrot's interpolated Z so the debug viz tracks
				// slopes; otherwise it'd float at chassis-level regardless
				// of where the carrot actually sits on a slope path.
				const FVector CarrotPos(LookAheadPoint.X.ToFloat(), LookAheadPoint.Y.ToFloat(), LookAheadPoint.Z.ToFloat() + 50.0f);
				DrawDebugPoint(DebugWorld, CarrotPos, 8.0f, FColor::Green, false, DrawLifetime);
				DrawDebugLine(DebugWorld, Origin, CarrotPos, FColor::Green, false, DrawLifetime, 0, 2.0f);
			}
		}
	}
#endif

	// -------------------------------------------------------------------
	// 5. Compute desired-yaw + apply this tick's rotation step (clamped
	//    by TurnRate). For tracked vehicles, the same TurnRate cap covers
	//    BOTH pivot-in-place and arc-while-driving — they don't have
	//    differentiated turn rates the way a wheeled bicycle model does.
	// -------------------------------------------------------------------
	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);

	// Reversing flips the steering target — we want the BACK pointed at the
	// goal, so chassis "forward" should aim away from it. The Arc/Pivot mode
	// split still reads `forward · effective_target_dir` correctly.
	const FFixedVector EffectiveToTarget = bIsReversing ? -ToTarget : ToTarget;

	FFixedPoint NewYaw = CurrentYaw;
	FFixedPoint AbsYawErr = FFixedPoint::Zero;   // raw |YawDelta|, before clamping — used by sharp-turn brake
	FFixedPoint AlignDotPostTurn = FFixedPoint::One; // assume aligned if no target

	if (EffectiveToTarget.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = SeinMath::Atan2(EffectiveToTarget.Y, EffectiveToTarget.X);
		const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		AbsYawErr = (YawDelta < FFixedPoint::Zero) ? -YawDelta : YawDelta;

		const FFixedPoint MaxTurn = MovementData.TurnRate * DeltaTime;
		const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
		NewYaw = CurrentYaw + AppliedTurn;

		// Post-turn alignment dot — used by pivot mode's "drive vs stand-and-turn" gate.
		const FFixedVector ToTargetN = FFixedVector::GetSafeNormal(EffectiveToTarget);
		const FFixedPoint NewFwdX = SeinMath::Cos(NewYaw);
		const FFixedPoint NewFwdY = SeinMath::Sin(NewYaw);
		AlignDotPostTurn = NewFwdX * ToTargetN.X + NewFwdY * ToTargetN.Y;
	}

	// -------------------------------------------------------------------
	// 6. Mode split — ARC mode (high speed) vs PIVOT mode (low speed).
	//
	//    ARC MODE  (AbsSpeed > PivotSpeed):
	//      Wheeled-like behavior. Drive at full throttle, optionally
	//      reduced by SharpTurnBrake for hard turns. The chassis arcs
	//      through misalignment instead of stopping to pivot — matches
	//      the wheeled vehicle feel for open-terrain U-turns.
	//
	//    PIVOT MODE (AbsSpeed ≤ PivotSpeed):
	//      Tracked-exclusive. If misaligned (dot < PivotAlignDot),
	//      throttle = 0 — chassis rotates in place at TurnRate. Once
	//      aligned, throttle = 1 — chassis accelerates from the pivot.
	//
	//    Tight terrain naturally drops speed via arrival cap / short-
	//    segment geometry, sliding the chassis into pivot mode without a
	//    separate "terrain tightness" check.
	// -------------------------------------------------------------------
	const FFixedPoint AbsSpeed = (CurrentSpeed < FFixedPoint::Zero) ? -CurrentSpeed : CurrentSpeed;

	FFixedPoint ThrottleScale = FFixedPoint::One;
	if (AbsSpeed > Tracked.PivotSpeed)
	{
		// ARC MODE. Full throttle baseline, optionally reduced by sharp-turn brake.
		//
		// Sharp-turn brake (matches wheeled): when |YawErr| > threshold,
		// throttle scales down by `SharpTurnBrakeStrength × AngleT × SpeedT`.
		// Velocity-gated so the brake only meaningfully fires at speed —
		// at AbsSpeed = PivotSpeed (the lower edge of arc mode) the SpeedT
		// factor is small → mostly full throttle. At TopSpeed → full brake
		// authority.
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
					SpeedT = AbsSpeed / MovementData.TopSpeed;
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
		// PIVOT MODE. Misaligned → stand and turn. Aligned → drive forward
		// from the pivot (accelerate from rest while still rotating). The
		// rotation step above already applied `TurnRate × Dt` regardless of
		// throttle, so the chassis is always actively orienting in this
		// mode — the throttle only gates translation.
		ThrottleScale = (AlignDotPostTurn < Tracked.PivotAlignDot)
			? FFixedPoint::Zero
			: FFixedPoint::One;
	}

	// -------------------------------------------------------------------
	// 7. Kinematic arrival cap (v² = 2·a·d).
	// -------------------------------------------------------------------
	FFixedPoint MaxArrivalSpeed;
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const FFixedPoint DistFinal = ToFinal.Size();
		MaxArrivalSpeed = KinematicArrivalSpeedCap(DistFinal, MovementData.Deceleration);
	}

	// -------------------------------------------------------------------
	// 8. Resolve target speed magnitude. Forward uses TopSpeed, reverse
	//    uses ReverseTopSpeed (or TopSpeed/2 fallback). Throttle scale
	//    applies to both modes; arrival cap clamps.
	// -------------------------------------------------------------------
	FFixedPoint MoveCap;
	if (bIsReversing)
	{
		MoveCap = (MovementData.ReverseTopSpeed > FFixedPoint::Zero)
			? MovementData.ReverseTopSpeed
			: MovementData.TopSpeed * FFixedPoint::Half;
	}
	else
	{
		MoveCap = MovementData.TopSpeed;
	}
	FFixedPoint TargetSpeedMag = MoveCap * ThrottleScale;
	if (MaxArrivalSpeed < TargetSpeedMag) TargetSpeedMag = MaxArrivalSpeed;
	const FFixedPoint TargetSpeed = bIsReversing ? -TargetSpeedMag : TargetSpeedMag;

	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		MovementData.Acceleration, MovementData.Deceleration, DeltaTime);

	// -------------------------------------------------------------------
	// 9. Translate along post-turn forward, nav-collision resolve, ground
	//    snap, slope-pitch/roll smoothing.
	// -------------------------------------------------------------------
	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	NewPos = ResolveNavCollision(AgentPos, NewPos, Nav);
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
		TEXT("Tracked: pos=(%.1f,%.1f) yaw=%.3f->%.3f mode=%s dot=%.3f yawErr=%.3f speed=%.2f->%.2f throttle=%.2f arrCap=%.1f rev=%d"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(),
		CurrentYaw.ToFloat(), NewYaw.ToFloat(),
		(AbsSpeed > Tracked.PivotSpeed) ? TEXT("ARC") : TEXT("PIVOT"),
		AlignDotPostTurn.ToFloat(),
		AbsYawErr.ToFloat(),
		EntryMag.ToFloat() * (EntryDot >= FFixedPoint::Zero ? 1.0f : -1.0f), CurrentSpeed.ToFloat(),
		ThrottleScale.ToFloat(), MaxArrivalSpeed.ToFloat(),
		bIsReversing ? 1 : 0);

	Entity.Transform.SetLocation(NewPos);
	// Persist velocity vector aligned with post-rotation forward.
	MovementData.Velocity = FFixedVector(CosY * CurrentSpeed, SinY * CurrentSpeed, FFixedPoint::Zero);

	return false;
}
