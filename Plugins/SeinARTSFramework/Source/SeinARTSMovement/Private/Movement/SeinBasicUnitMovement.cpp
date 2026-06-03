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

	const FFixedVector InitialPos = Entity.Transform.GetLocation();
	FFixedVector Pos = InitialPos;
	FFixedPoint RemainingStep = MovementData.TopSpeed * DeltaTime;

	while (RemainingStep > FFixedPoint::Zero && CurrentWaypointIndex < Path.Waypoints.Num())
	{
		const FFixedVector Target = Path.Waypoints[CurrentWaypointIndex];
		FFixedVector Delta = Target - Pos;
		Delta.Z = FFixedPoint::Zero;
		const FFixedPoint DistSq = Delta.SizeSquared();

		const bool bIsFinalWaypoint = (CurrentWaypointIndex == Path.Waypoints.Num() - 1);
		const FFixedPoint OneStep = MovementData.TopSpeed * DeltaTime;
		const FFixedPoint ArriveRadiusSq = bIsFinalWaypoint ? AcceptanceRadiusSq : OneStep * OneStep;

		if (DistSq <= ArriveRadiusSq)
		{
			Pos.X = Target.X;
			Pos.Y = Target.Y;
			++CurrentWaypointIndex;
			continue;
		}

		const FFixedPoint Dist = Delta.Size();
		const FFixedPoint StepLen = (Dist < RemainingStep) ? Dist : RemainingStep;

		const FFixedVector Dir = FFixedVector::GetSafeNormal(Delta);

		Pos.X = Pos.X + Dir.X * StepLen;
		Pos.Y = Pos.Y + Dir.Y * StepLen;

		RemainingStep = RemainingStep - StepLen;
	}

	Pos = ResolveNavCollision(InitialPos, Pos, Nav);

	ApplyGroundSnapAndAltitude(Pos, Ctx.MovementData, Nav, DeltaTime);

	Entity.Transform.SetLocation(Pos);

	// Smooth turn-to-velocity from the actual moved delta. Velocity-based
	// (rather than waypoint-direction-based) means a blocked tick naturally
	// holds facing.
	FFixedVector Delta = Pos - InitialPos;
	Delta.Z = FFixedPoint::Zero;
	if (Delta.SizeSquared() > FFixedPoint::Epsilon)
	{
		const FFixedPoint DesiredYaw = SeinMath::Atan2(Delta.Y, Delta.X);
		const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);
		const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		const FFixedPoint MaxTurn = MovementData.TurnRate * DeltaTime;
		const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
		const FFixedPoint FinalYaw = CurrentYaw + AppliedTurn;
		// Rate-limited pitch/roll smoothing — see comment in SeinInfantryMovement.
		const FFixedPoint TargetPitch = ComputeSlopePitch(Pos, FinalYaw, Nav);
		const FFixedPoint TargetRoll  = ComputeSlopeRoll(Pos, FinalYaw, Nav);
		const FFixedPoint OrientRate  = FFixedPoint::Pi / FFixedPoint::FromInt(3); // 60°/sec
		MovementData.SmoothedPitch = SmoothAngleToward(MovementData.SmoothedPitch, TargetPitch, OrientRate, DeltaTime);
		MovementData.SmoothedRoll  = SmoothAngleToward(MovementData.SmoothedRoll,  TargetRoll,  OrientRate, DeltaTime);
		Entity.Transform.Rotation = YawPitchRoll(FinalYaw, MovementData.SmoothedPitch, MovementData.SmoothedRoll);
	}

	// Persist world-frame velocity. BasicUnit is non-strafing -- facing tracks
	// movement direction, so velocity is forward x scalar speed.
	if (DeltaTime > FFixedPoint::Zero)
	{
		const FFixedPoint Scalar = Delta.Size() / DeltaTime;
		const FFixedVector NewForward = Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);
		MovementData.Velocity = FFixedVector(NewForward.X * Scalar, NewForward.Y * Scalar, FFixedPoint::Zero);
	}
	else
	{
		MovementData.Velocity = FFixedVector::ZeroVector;
	}

	UE_LOG(LogSeinBasicUnit, Verbose,
		TEXT("BasicUnit: pre=(%.2f,%.2f) post=(%.2f,%.2f) wp[%d/%d] yaw=%.3f"),
		InitialPos.X.ToFloat(), InitialPos.Y.ToFloat(),
		Pos.X.ToFloat(), Pos.Y.ToFloat(),
		CurrentWaypointIndex, Path.Waypoints.Num(),
		YawFromRotation(Entity.Transform.Rotation).ToFloat());

	return CurrentWaypointIndex >= Path.Waypoints.Num();
}
