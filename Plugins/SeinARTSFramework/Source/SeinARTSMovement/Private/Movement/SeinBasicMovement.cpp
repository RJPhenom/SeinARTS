/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicMovement.cpp
 */

#include "Movement/SeinBasicMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Components/SeinMovementComponent.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"

bool USeinBasicMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData)
	{
		// No movement component on the entity — nothing to drive. Report
		// arrived so the move-to action ends cleanly rather than spinning.
		return true;
	}

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
	// Local avoidance applies to this tick's FIRST movement step only.
	bool bAvoidanceApplied = false;

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

	if (DeltaTime > FFixedPoint::Zero)
	{
		FFixedVector Disp = Pos - InitialPos;
		MovementData.Velocity = FFixedVector(Disp.X / DeltaTime, Disp.Y / DeltaTime, FFixedPoint::Zero);
	}
	else
	{
		MovementData.Velocity = FFixedVector::ZeroVector;
	}

	return CurrentWaypointIndex >= Path.Waypoints.Num();
}
