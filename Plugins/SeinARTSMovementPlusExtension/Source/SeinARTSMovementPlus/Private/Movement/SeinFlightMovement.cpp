/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFlightMovement.cpp
 */

#include "Movement/SeinFlightMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementComponent.h"
#include "Data/SeinFlyingMovementData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFlight, Log, All);

UScriptStruct* USeinFlightMovement::GetMovementDataStruct() const
{
	return FSeinFlyingMovementData::StaticStruct();
}

FFixedPoint USeinFlightMovement::GetDeceleration(const FSeinMovementComponent* MovementData) const
{
	const FSeinFlyingMovementData* Data = MovementData ? MovementData->MovementClassData.GetPtr<FSeinFlyingMovementData>() : nullptr;
	return Data ? Data->Deceleration : FSeinFlyingMovementData().Deceleration;
}

FFixedPoint USeinFlightMovement::GetAltitude(const FSeinMovementComponent* MovementData) const
{
	if (!MovementData) return FFixedPoint::Zero;
	if (const FSeinFlyingMovementData* FlightData = MovementData->MovementClassData.GetPtr<FSeinFlyingMovementData>())
	{
		return FlightData->Altitude;
	}
	return FFixedPoint::Zero;
}

bool USeinFlightMovement::QueryReferenceZ(USeinNavigation* Nav, const FFixedVector& WorldPos, FFixedPoint& OutZ) const
{
	return Nav ? Nav->GetCellHeightAt(WorldPos, OutZ, /*bWalkableOnly=*/ false) : false;
}

void USeinFlightMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// Reset bank — wings level at start of each order. Speed and altitude
	// preserved across reorders (momentum + cruise-altitude continuity).
	CurrentSteer = FFixedPoint::Zero;

	if (!Ctx.MovementData) return;

	// If the plane is starting from rest (e.g., first move after spawn),
	// bump velocity magnitude to the loiter minimum immediately. A plane at
	// 0 speed in mid-air would visually fall and the first-tick math would
	// underflow.
	const FSeinFlyingMovementData DefaultsFlying;
	const FSeinFlyingMovementData* FlyingPtr = Ctx.MovementData->MovementClassData.GetPtr<FSeinFlyingMovementData>();
	const FSeinFlyingMovementData& FlyingData = FlyingPtr ? *FlyingPtr : DefaultsFlying;
	const FFixedPoint MinSpeed = Ctx.MovementData->TopSpeed * FlyingData.MinSpeedRatio;
	const FFixedPoint VelMag = Ctx.MovementData->Velocity.Size();
	if (VelMag < MinSpeed)
	{
		const FFixedVector Forward = Ctx.Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
		Ctx.MovementData->Velocity = FFixedVector(Forward.X * MinSpeed, Forward.Y * MinSpeed, FFixedPoint::Zero);
	}
}

bool USeinFlightMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	// Per-class tuning (accel/decel live here now, off the bare component). Defaults when unauthored.
	const FSeinFlyingMovementData DefaultsFlying;
	const FSeinFlyingMovementData* FlyingPtr = MovementData.MovementClassData.GetPtr<FSeinFlyingMovementData>();
	const FSeinFlyingMovementData& FlyingData = FlyingPtr ? *FlyingPtr : DefaultsFlying;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadiusSq = Ctx.AcceptanceRadiusSq;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	USeinNavigation* Nav = Ctx.Nav;

	const int32 N = Path.Waypoints.Num();
	if (N == 0) return true;

	// Recover signed scalar speed from persisted velocity vector.
	const FFixedQuaternion EntryRot = Entity.Transform.Rotation;
	const FFixedVector EntryForward = EntryRot.RotateVector(FFixedVector::ForwardVector);
	const FFixedPoint EntryDot = MovementData.Velocity.X * EntryForward.X + MovementData.Velocity.Y * EntryForward.Y;
	const FFixedPoint EntryMag = MovementData.Velocity.Size();
	FFixedPoint CurrentSpeed = (EntryDot >= FFixedPoint::Zero) ? EntryMag : -EntryMag;

	const FFixedVector AgentPos = Entity.Transform.GetLocation();
	const FFixedVector FinalWp = Path.Waypoints[N - 1];

	// XY arrival. Speed is NOT zeroed — minimum speed constraint means
	// the plane keeps flying even after the action ends.
	{
		FFixedVector ToFinal = FinalWp - AgentPos;
		ToFinal.Z = FFixedPoint::Zero;
		const bool bWithinAcceptance = ToFinal.SizeSquared() <= AcceptanceRadiusSq;

		// Wider overshoot vicinity for planes — they can't slow precisely.
		const FFixedPoint VicinityRadiusSq = AcceptanceRadiusSq * FFixedPoint::FromInt(6);
		const FFixedPoint OvershootSpeedCap = MovementData.TopSpeed; // any speed counts
		const bool bOvershoot = IsOvershootArrival(
			AgentPos, FinalWp, Entity.Transform.Rotation,
			CurrentSpeed, VicinityRadiusSq, OvershootSpeedCap);

		if (bWithinAcceptance || bOvershoot)
		{
			// Don't zero velocity — leave at min loiter so the plane drifts
			// past, ready for the next order with momentum.
			const FFixedPoint MinSpeed = MovementData.TopSpeed * FlyingData.MinSpeedRatio;
			const FFixedPoint CurMag = MovementData.Velocity.Size();
			if (CurMag < MinSpeed)
			{
				MovementData.Velocity = FFixedVector(EntryForward.X * MinSpeed, EntryForward.Y * MinSpeed, FFixedPoint::Zero);
			}
			return true;
		}
	}

	// Steering target on the polyline.
	const FFixedPoint LookAhead = (FlyingData.LookAheadDistance > FFixedPoint::Zero)
		? FlyingData.LookAheadDistance : FFixedPoint::FromInt(200);
	const FFixedVector LookAheadPoint = ResolveLookAheadPoint(AgentPos, Path, CurrentWaypointIndex, LookAhead);

	FFixedVector ToTarget = LookAheadPoint - AgentPos;
	ToTarget.Z = FFixedPoint::Zero;

	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);

	// Aim error -> desired bank angle.
	FFixedPoint DesiredSteer = FFixedPoint::Zero;
	if (ToTarget.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = SeinMath::Atan2(ToTarget.Y, ToTarget.X);
		const FFixedPoint YawErr = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		DesiredSteer = ClampFP(YawErr, -FlyingData.MaxSteerAngle, FlyingData.MaxSteerAngle);
	}

	// Smooth bank toward desired.
	{
		FFixedPoint Alpha = FlyingData.SteerResponse * DeltaTime;
		if (Alpha < FFixedPoint::Zero) Alpha = FFixedPoint::Zero;
		if (Alpha > FFixedPoint::One)  Alpha = FFixedPoint::One;
		CurrentSteer = CurrentSteer + (DesiredSteer - CurrentSteer) * Alpha;
	}

	// Bicycle yaw rate: omega = (v / L) * tan(delta). Capped by TurnRate
	// as a safety lid on extreme combos.
	FFixedPoint YawStep = FFixedPoint::Zero;
	if (FlyingData.Wheelbase > FFixedPoint::One)
	{
		const FFixedPoint TanSteer = SeinMath::Tan(CurrentSteer);
		const FFixedPoint YawRate = (CurrentSpeed / FlyingData.Wheelbase) * TanSteer;
		const FFixedPoint MaxRate = MovementData.TurnRate;
		const FFixedPoint ClampedRate = ClampFP(YawRate, -MaxRate, MaxRate);
		YawStep = ClampedRate * DeltaTime;
	}
	const FFixedPoint NewYaw = CurrentYaw + YawStep;
	Entity.Transform.Rotation = YawOnly(NewYaw);

	// Speed: target = MoveSpeed; floor = MoveSpeed * MinSpeedRatio.
	// No kinematic arrival cap — planes don't slow on approach.
	const FFixedPoint MinSpeed = MovementData.TopSpeed * FlyingData.MinSpeedRatio;
	FFixedPoint TargetSpeed = MovementData.TopSpeed;
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		FlyingData.Acceleration, FlyingData.Deceleration, DeltaTime);
	if (CurrentSpeed < MinSpeed) CurrentSpeed = MinSpeed;

	// Translate along post-rotation forward.
	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	// Altitude: lerp toward max(cruise, clearance). Same shape as Hover —
	// current altitude lives on FSeinFlyingMovementData so it persists
	// across move orders.
	const FFixedPoint TargetAltitude = (FlyingData.CruiseAltitude > FlyingData.AltitudeClearanceThreshold)
		? FlyingData.CruiseAltitude : FlyingData.AltitudeClearanceThreshold;
	FFixedPoint CurrentAltitude = FFixedPoint::Zero;
	if (FSeinFlyingMovementData* FlightData = MovementData.MovementClassData.GetMutablePtr<FSeinFlyingMovementData>())
	{
		const FFixedPoint AltStep = FlyingData.AltitudeChangeRate * DeltaTime;
		const FFixedPoint AltDelta = TargetAltitude - FlightData->Altitude;
		if (AltDelta > AltStep)        FlightData->Altitude = FlightData->Altitude + AltStep;
		else if (AltDelta < -AltStep)  FlightData->Altitude = FlightData->Altitude - AltStep;
		else                            FlightData->Altitude = TargetAltitude;
		CurrentAltitude = FlightData->Altitude;
	}

	// Z snap via QueryReferenceZ override -> GetCellHeightAt(false).
	// Auto-clears whatever's in the cell. No ResolveNavCollision — planes
	// fly through.
	ApplyGroundSnapAndAltitude(NewPos, Ctx.MovementData, Nav, DeltaTime);

	UE_LOG(LogSeinFlight, Verbose,
		TEXT("Flight: pos=(%.1f,%.1f,%.1f) yaw=%.3f steer=%.3f speed=%.2f->%.2f alt=%.1f->%.1f"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(), AgentPos.Z.ToFloat(),
		NewYaw.ToFloat(), CurrentSteer.ToFloat(),
		EntryMag.ToFloat() * (EntryDot >= FFixedPoint::Zero ? 1.0f : -1.0f), CurrentSpeed.ToFloat(),
		CurrentAltitude.ToFloat(), TargetAltitude.ToFloat());

	Entity.Transform.SetLocation(NewPos);
	// Persist velocity vector aligned with post-rotation forward.
	MovementData.Velocity = FFixedVector(CosY * CurrentSpeed, SinY * CurrentSpeed, FFixedPoint::Zero);

	return false;
}
