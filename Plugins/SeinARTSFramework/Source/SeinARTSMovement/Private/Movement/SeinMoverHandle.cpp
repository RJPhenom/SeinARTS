/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoverHandle.cpp
 */

#include "Movement/SeinMoverHandle.h"
#include "Movement/SeinMovement.h"   // FSeinMovementContext
#include "Components/SeinMovementComponent.h"
#include "Types/Entity.h"
#include "SeinPathTypes.h"

void USeinMoverHandle::SetContext(const FSeinMovementContext* InCtx)
{
	Ctx = InCtx;
	EntityPtr = InCtx ? &InCtx->Entity : nullptr;
}

void USeinMoverHandle::SetEntityOnly(FSeinEntity* InEntity)
{
	Ctx = nullptr;
	EntityPtr = InEntity;
}

bool USeinMoverHandle::IsValidMover() const
{
	return Ctx != nullptr && Ctx->MovementData != nullptr;
}

// ---- Transform ----------------------------------------------------------------

FFixedVector USeinMoverHandle::GetLocation() const
{
	return EntityPtr ? EntityPtr->Transform.GetLocation() : FFixedVector::ZeroVector;
}

void USeinMoverHandle::SetLocation(const FFixedVector& NewLocation)
{
	if (EntityPtr) EntityPtr->Transform.SetLocation(NewLocation);
}

FFixedQuaternion USeinMoverHandle::GetRotation() const
{
	return EntityPtr ? EntityPtr->Transform.Rotation : FFixedQuaternion::Identity;
}

void USeinMoverHandle::SetRotation(const FFixedQuaternion& NewRotation)
{
	if (EntityPtr) EntityPtr->Transform.Rotation = NewRotation;
}

// ---- Velocity -----------------------------------------------------------------

FFixedVector USeinMoverHandle::GetVelocity() const
{
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->Velocity : FFixedVector::ZeroVector;
}

void USeinMoverHandle::SetVelocity(const FFixedVector& NewVelocity)
{
	if (Ctx && Ctx->MovementData) Ctx->MovementData->Velocity = NewVelocity;
}

FFixedPoint USeinMoverHandle::GetSpeed() const
{
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->Velocity.Size() : FFixedPoint::Zero;
}

// ---- Authored kinematics ------------------------------------------------------

FFixedPoint USeinMoverHandle::GetTopSpeed() const
{
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->TopSpeed : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetAcceleration() const
{
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->Acceleration : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetDeceleration() const
{
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->Deceleration : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetTurnRate() const
{
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->TurnRate : FFixedPoint::Zero;
}

// ---- Per-tick inputs ----------------------------------------------------------

FFixedPoint USeinMoverHandle::GetDeltaTime() const
{
	return Ctx ? Ctx->DeltaTime : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetTerrainSpeedMultiplier() const
{
	return Ctx ? Ctx->TerrainSpeedMultiplier : FFixedPoint::One;
}

FFixedPoint USeinMoverHandle::GetAcceptanceRadiusSq() const
{
	return Ctx ? Ctx->AcceptanceRadiusSq : FFixedPoint::Zero;
}

// ---- Path / waypoints ---------------------------------------------------------

int32 USeinMoverHandle::GetWaypointCount() const
{
	return Ctx ? Ctx->Path.Waypoints.Num() : 0;
}

int32 USeinMoverHandle::GetCurrentWaypointIndex() const
{
	return Ctx ? Ctx->CurrentWaypointIndex : 0;
}

void USeinMoverHandle::SetCurrentWaypointIndex(int32 Index)
{
	if (Ctx) Ctx->CurrentWaypointIndex = Index;
}

FFixedVector USeinMoverHandle::GetWaypoint(int32 Index) const
{
	if (!Ctx || !Ctx->Path.Waypoints.IsValidIndex(Index)) return FFixedVector::ZeroVector;
	return Ctx->Path.Waypoints[Index];
}

FFixedVector USeinMoverHandle::GetCurrentWaypoint() const
{
	return GetWaypoint(Ctx ? Ctx->CurrentWaypointIndex : 0);
}

FFixedVector USeinMoverHandle::GetFinalWaypoint() const
{
	if (!Ctx || Ctx->Path.Waypoints.Num() == 0) return FFixedVector::ZeroVector;
	return Ctx->Path.Waypoints.Last();
}

FFixedPoint USeinMoverHandle::GetDistanceToFinal() const
{
	if (!Ctx || Ctx->Path.Waypoints.Num() == 0) return FFixedPoint::Zero;
	FFixedVector ToFinal = Ctx->Path.Waypoints.Last() - Ctx->Entity.Transform.GetLocation();
	ToFinal.Z = FFixedPoint::Zero;
	return ToFinal.Size();
}

// ---- Steering toolkit (Tier 2) ------------------------------------------------

USeinMovement* USeinMoverHandle::GetOwningMovement() const
{
	return GetTypedOuter<USeinMovement>();
}

FFixedPoint USeinMoverHandle::GetEffectiveTopSpeed() const
{
	return Ctx ? USeinMovement::EffectiveTopSpeed(*Ctx) : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::StepSpeedToward(FFixedPoint Current, FFixedPoint Target, FFixedPoint Acceleration, FFixedPoint Deceleration) const
{
	return USeinMovement::StepSpeedToward(Current, Target, Acceleration, Deceleration, GetDeltaTime());
}

FFixedPoint USeinMoverHandle::KinematicArrivalSpeedCap(FFixedPoint DistanceToStop, FFixedPoint Deceleration) const
{
	return USeinMovement::KinematicArrivalSpeedCap(DistanceToStop, Deceleration);
}

FFixedPoint USeinMoverHandle::ComputeAdaptiveLookAhead(FFixedPoint BaseDistance, FFixedPoint TimeHorizon, FFixedPoint AbsSpeed) const
{
	return USeinMovement::ComputeAdaptiveLookAhead(BaseDistance, TimeHorizon, AbsSpeed);
}

FFixedVector USeinMoverHandle::ResolveLookAheadPoint(FFixedPoint LookAhead) const
{
	if (!Ctx) return GetLocation();
	return USeinMovement::ResolveLookAheadPoint(GetLocation(), Ctx->Path, Ctx->CurrentWaypointIndex, LookAhead);
}

void USeinMoverHandle::AdvanceWaypoint(FFixedPoint CloseRadius)
{
	if (!Ctx) return;
	USeinMovement::AdvanceWaypointAlongPath(Ctx->CurrentWaypointIndex, Ctx->Path, GetLocation(), CloseRadius);
}

FFixedPoint USeinMoverHandle::ShortestAngleDelta(FFixedPoint From, FFixedPoint To) const
{
	return USeinMovement::ShortestAngleDelta(From, To);
}

FFixedPoint USeinMoverHandle::SmoothAngleToward(FFixedPoint Current, FFixedPoint Target, FFixedPoint MaxRatePerSec) const
{
	return USeinMovement::SmoothAngleToward(Current, Target, MaxRatePerSec, GetDeltaTime());
}

FFixedVector USeinMoverHandle::ApplyAvoidanceSteer(FFixedVector DesiredDir) const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->ApplyAvoidanceSteer(*Ctx, DesiredDir) : DesiredDir;
}

FFixedVector USeinMoverHandle::ResolveNavCollision(FFixedVector OldPos, FFixedVector NewPos) const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->ResolveNavCollision(OldPos, NewPos, Ctx->Nav) : NewPos;
}

FFixedVector USeinMoverHandle::ApplyGroundSnapAndAltitude(FFixedVector Pos) const
{
	USeinMovement* Owner = GetOwningMovement();
	if (Owner && Ctx)
	{
		Owner->ApplyGroundSnapAndAltitude(Pos, Ctx->MovementData, Ctx->Nav, Ctx->DeltaTime);
	}
	return Pos;
}

FFixedPoint USeinMoverHandle::ComputeSlopePitch(FFixedVector Pos, FFixedPoint Yaw) const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->ComputeSlopePitch(Pos, Yaw, Ctx->Nav) : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::ComputeSlopeRoll(FFixedVector Pos, FFixedPoint Yaw) const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->ComputeSlopeRoll(Pos, Yaw, Ctx->Nav) : FFixedPoint::Zero;
}

bool USeinMoverHandle::ShouldAutoReverse(FFixedVector FinalGoal) const
{
	if (!Ctx || !Ctx->MovementData) return false;
	return USeinMovement::ShouldAutoReverse(GetLocation(), GetRotation(), FinalGoal, *Ctx->MovementData);
}
