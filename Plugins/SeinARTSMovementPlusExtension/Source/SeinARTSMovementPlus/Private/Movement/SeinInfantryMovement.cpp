/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinInfantryMovement.cpp
 */

#include "Movement/SeinInfantryMovement.h"
#include "Data/SeinInfantryMovementData.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementComponent.h"
#include "Movement/SeinMoverHandle.h"

UScriptStruct* USeinInfantryMovement::GetMovementDataStruct() const
{
	return FSeinInfantryMovementData::StaticStruct();
}

FFixedPoint USeinInfantryMovement::GetDeceleration(const FSeinMovementComponent* MovementData) const
{
	const FSeinInfantryMovementData* Data = MovementData ? MovementData->MovementClassData.GetPtr<FSeinInfantryMovementData>() : nullptr;
	return Data ? Data->Deceleration : FSeinInfantryMovementData().Deceleration;
}

void USeinInfantryMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// MovementData.Velocity is intentionally preserved — momentum carries across order changes. With
	// high Acceleration / Deceleration the transition from one velocity to another is near-instant but
	// never a hard zero-snap.
}

FSeinMotion USeinInfantryMovement::ComputeMotion_Implementation(USeinMoverHandle* Mover)
{
	// The "nice" ground default: a momentum-aware seek with an alignment-scaled speed that produces the
	// natural decel-rotate-accel arc on sharp turns. The unit walks along its CURRENT facing
	// (non-strafing) at a speed scaled by how aligned that facing is with the desired direction, then
	// rotates toward the desired direction — so a 90°+ order slows the unit to a near-stop, it pivots,
	// then accelerates away. Speed accelerates/decelerates (StepSpeedToward) and brakes into the final
	// waypoint (kinematic arrival cap). The base Tick harness owns arrival, the nav floor, ground snap,
	// the TurnRate-clamped turn, slope tilt, and velocity persistence.
	FSeinMotion Motion;
	const FSeinMovementContext* C = Mover ? Mover->GetContext() : nullptr;
	if (!C || !C->MovementData) return Motion;
	const FSeinMovementContext& Ctx = *C;

	const FSeinPath& Path = Ctx.Path;
	const int32 N = Path.Waypoints.Num();
	if (N == 0 || Ctx.CurrentWaypointIndex >= N) return Motion;

	const FSeinMovementComponent& MovementData = *Ctx.MovementData;
	// Per-class tuning (accel/decel live here now, off the bare component). Defaults when unauthored.
	const FSeinInfantryMovementData Defaults;
	const FSeinInfantryMovementData* DataPtr = MovementData.MovementClassData.GetPtr<FSeinInfantryMovementData>();
	const FSeinInfantryMovementData& Data = DataPtr ? *DataPtr : Defaults;
	const FFixedPoint DeltaTime = Ctx.DeltaTime;
	const FFixedVector Pos = Ctx.Entity.Transform.GetLocation();
	const FFixedVector Forward = Ctx.Entity.Transform.Rotation.RotateVector(FFixedVector::ForwardVector);

	// Desired direction toward the current waypoint, bent by local avoidance.
	FFixedVector Waypoint = Path.Waypoints[Ctx.CurrentWaypointIndex];
	Waypoint.Z = Pos.Z;
	const bool bHasDirection = !FFixedVector::IsPlanarDistanceWithin(
		Pos, Waypoint, FFixedPoint::Epsilon);
	FFixedVector Dir = FFixedVector::ZeroVector;
	if (bHasDirection)
	{
		Dir = ApplyAvoidanceSteer(
			Ctx, FFixedVector::GetSafeNormalDifference(Pos, Waypoint));
	}

	// Alignment-scaled cruise: full speed when facing the desired dir, zero at 90°+ off (so the unit
	// decelerates and rotates in place). Scaled by the avoidance speed-yield (no-op unless a model writes it).
	const FFixedPoint AlignDot = (Dir.SizeSquared() > FFixedPoint::Epsilon)
		? FFixedVector::DotProduct(Dir, Forward) : FFixedPoint::Zero;
	const FFixedPoint Alignment = (AlignDot > FFixedPoint::Zero) ? AlignDot : FFixedPoint::Zero;
	FFixedPoint TargetSpeed = EffectiveTopSpeed(Ctx) * Alignment * GetAvoidanceSpeedScale(Ctx);

	// Kinematic arrival brake against the final waypoint (v² = 2·a·d).
	{
		FFixedVector FinalWaypoint = Path.Waypoints[N - 1];
		FinalWaypoint.Z = Pos.Z;
		const FFixedPoint MaxArrival = KinematicArrivalSpeedCap(
			FFixedVector::DistanceSaturated(Pos, FinalWaypoint),
			Data.Deceleration);
		if (MaxArrival < TargetSpeed) TargetSpeed = MaxArrival;
	}

	// Accel/decel ramp from the persisted speed (momentum across ticks + orders).
	const FFixedPoint EntrySpeed = MovementData.Velocity.Size();
	const FFixedPoint Speed = StepSpeedToward(EntrySpeed, TargetSpeed,
		Data.Acceleration, Data.Deceleration, DeltaTime);

	// Move along CURRENT forward (non-strafing); rotate toward the desired dir. Skip the facing update
	// when there is no desired direction so a waypoint-on-top-of-unit tick doesn't slew facing.
	Motion.Velocity = FFixedVector(Forward.X * Speed, Forward.Y * Speed, FFixedPoint::Zero);
	if (Dir.SizeSquared() > FFixedPoint::Epsilon)
	{
		Motion.TargetYaw = SeinMath::Atan2(Dir.Y, Dir.X);
		Motion.bUpdateFacing = true;
	}
	return Motion;
}
