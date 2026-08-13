/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinHoverMovement.cpp
 */

#include "Movement/SeinHoverMovement.h"
#include "SeinNavigation.h"
#include "SeinPathTypes.h"
#include "Math/MathLib.h"
#include "Types/Entity.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "Components/SeinMovementComponent.h"
#include "Data/SeinHoverMovementData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinHover, Log, All);

namespace
{
	FFixedPoint ScalePositiveRadius(FFixedPoint Radius, FFixedPoint Scale)
	{
		if (Radius > FFixedPoint::Zero && Scale > FFixedPoint::Zero
			&& Radius > FFixedPoint::MaxValue / Scale)
		{
			return FFixedPoint::MaxValue;
		}
		return Radius * Scale;
	}
}

UScriptStruct* USeinHoverMovement::GetMovementDataStruct() const
{
	return FSeinHoverMovementData::StaticStruct();
}

FFixedPoint USeinHoverMovement::GetDeceleration(const FSeinMovementComponent* MovementData) const
{
	const FSeinHoverMovementData* Data = MovementData ? MovementData->MovementClassData.GetPtr<FSeinHoverMovementData>() : nullptr;
	return Data ? Data->Deceleration : FSeinHoverMovementData().Deceleration;
}

FFixedPoint USeinHoverMovement::GetAltitude(const FSeinMovementComponent* MovementData) const
{
	if (!MovementData) return FFixedPoint::Zero;
	if (const FSeinHoverMovementData* HoverData = MovementData->MovementClassData.GetPtr<FSeinHoverMovementData>())
	{
		return HoverData->Altitude;
	}
	return FFixedPoint::Zero;
}

bool USeinHoverMovement::QueryReferenceZ(USeinNavigation* Nav, const FFixedVector& WorldPos, FFixedPoint& OutZ) const
{
	// bWalkableOnly=false — top-of-surface Z for any cell. Altitude above
	// the surface auto-clears whatever's in the cell (building roof, wall
	// top, cliff face) without per-cell clearance math.
	return Nav ? Nav->GetCellHeightAt(WorldPos, OutZ, /*bWalkableOnly=*/ false) : false;
}

void USeinHoverMovement::OnMoveBegin(const FSeinMovementContext& Ctx)
{
	// Preserve MovementData.Velocity (momentum carries across reorders) and
	// FSeinHoverMovementData::Altitude (don't slam to 0 — the helicopter is
	// already up there from a previous order or initial spawn).
}

bool USeinHoverMovement::Tick(const FSeinMovementContext& Ctx)
{
	if (!Ctx.MovementData) return true;

	FSeinEntity& Entity = Ctx.Entity;
	FSeinMovementComponent& MovementData = *Ctx.MovementData;
	// Per-class tuning (accel/decel live here now, off the bare component). Defaults when unauthored.
	const FSeinHoverMovementData DefaultsHover;
	const FSeinHoverMovementData* HoverPtr = MovementData.MovementClassData.GetPtr<FSeinHoverMovementData>();
	const FSeinHoverMovementData& HoverTuning = HoverPtr ? *HoverPtr : DefaultsHover;
	const FSeinPath& Path = Ctx.Path;
	int32& CurrentWaypointIndex = Ctx.CurrentWaypointIndex;
	const FFixedPoint AcceptanceRadius = Ctx.GetAcceptanceRadius();
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

	// XY-only arrival check. Altitude is independent.
	{
		const bool bWithinAcceptance =
			Ctx.IsWithinPlanarAcceptance(AgentPos, FinalWp);

		const FFixedPoint VicinityRadius = ScalePositiveRadius(
			AcceptanceRadius, FFixedPoint::Two);
		const FFixedPoint OvershootSpeedCap = MovementData.TopSpeed / FFixedPoint::FromInt(3);
		const bool bOvershoot = Ctx.HasExactAcceptanceRadius()
			? IsOvershootArrivalRadius(
				AgentPos, FinalWp, Entity.Transform.Rotation,
				CurrentSpeed, VicinityRadius, OvershootSpeedCap)
			: IsOvershootArrival(
				AgentPos, FinalWp, Entity.Transform.Rotation,
				CurrentSpeed,
				ScalePositiveRadius(
					Ctx.AcceptanceRadiusSq, FFixedPoint::FromInt(4)),
				OvershootSpeedCap);

		if (bWithinAcceptance || bOvershoot)
		{
			// Hover stops at destination.
			MovementData.Velocity = FFixedVector::ZeroVector;
			return true;
		}
	}

	// Steering target on the (straight-line) polyline.
	const FFixedPoint LookAhead = (HoverTuning.LookAheadDistance > FFixedPoint::Zero)
		? HoverTuning.LookAheadDistance : FFixedPoint::FromInt(100);
	FFixedVector LookAheadPoint = ResolveLookAheadPoint(
		AgentPos, Path, CurrentWaypointIndex, LookAhead);
	LookAheadPoint.Z = AgentPos.Z;
	const bool bHasTarget = !FFixedVector::IsPlanarDistanceWithin(
		AgentPos, LookAheadPoint, FFixedPoint::Epsilon);
	FFixedVector ToTarget = bHasTarget
		? FFixedVector::GetSafeNormalDifference(AgentPos, LookAheadPoint)
		: FFixedVector::ZeroVector;

	// Local avoidance — bend the carrot direction around nearby units. Normalized
	// in/out (angular effect independent of look-ahead distance); only the yaw target
	// below consumes ToTarget. Soft layer; the floor still guarantees no overlap.
	if (bHasTarget)
	{
		ToTarget = ApplyAvoidanceSteer(Ctx, FFixedVector::GetSafeNormal(ToTarget));
	}

	const FFixedPoint CurrentYaw = YawFromRotation(Entity.Transform.Rotation);

	// Smooth turn-to-target at TurnRate. Hover has no bicycle turning
	// constraint — it can pivot in place at full TurnRate regardless of
	// speed. Same model as Infantry, just with airborne Z handling.
	FFixedPoint NewYaw = CurrentYaw;
	if (bHasTarget)
	{
		const FFixedPoint DesiredYaw = SeinMath::Atan2(ToTarget.Y, ToTarget.X);
		const FFixedPoint YawDelta = ShortestAngleDelta(CurrentYaw, DesiredYaw);
		const FFixedPoint MaxTurn = MovementData.TurnRate * DeltaTime;
		const FFixedPoint AppliedTurn = ClampFP(YawDelta, -MaxTurn, MaxTurn);
		NewYaw = CurrentYaw + AppliedTurn;
	}
	Entity.Transform.Rotation = YawOnly(NewYaw);

	// Speed ramp toward MoveSpeed, capped by kinematic arrival brake.
	FFixedVector PlanarFinal = FinalWp;
	PlanarFinal.Z = AgentPos.Z;
	const FFixedPoint DistFinal =
		FFixedVector::DistanceSaturated(AgentPos, PlanarFinal);
	const FFixedPoint MaxArrivalSpeed = KinematicArrivalSpeedCap(DistFinal, HoverTuning.Deceleration);
	FFixedPoint TargetSpeed = MovementData.TopSpeed;
	if (MaxArrivalSpeed < TargetSpeed) TargetSpeed = MaxArrivalSpeed;
	CurrentSpeed = StepSpeedToward(CurrentSpeed, TargetSpeed,
		HoverTuning.Acceleration, HoverTuning.Deceleration, DeltaTime);

	// Translate along post-rotation forward.
	const FFixedPoint CosY = SeinMath::Cos(NewYaw);
	const FFixedPoint SinY = SeinMath::Sin(NewYaw);
	const FFixedPoint StepLen = CurrentSpeed * DeltaTime;
	FFixedVector NewPos = AgentPos;
	NewPos.X = NewPos.X + CosY * StepLen;
	NewPos.Y = NewPos.Y + SinY * StepLen;

	// XY nav collision — hover still respects ground nav for XY positioning.
	NewPos = ResolveNavCollision(AgentPos, NewPos, Nav);

	// Altitude target: max(cruise, clearance floor). Smooth toward it at
	// AltitudeChangeRate. The current altitude lives on the polymorphic
	// per-class sub-data so it persists across move orders — a helicopter
	// holds altitude between commands. Falls back to a no-op lerp when no
	// HoverData is authored (GetAltitude returns 0 in that case).
	const FFixedPoint TargetAltitude = (HoverTuning.CruiseAltitude > HoverTuning.AltitudeClearanceThreshold)
		? HoverTuning.CruiseAltitude : HoverTuning.AltitudeClearanceThreshold;
	FFixedPoint CurrentAltitude = FFixedPoint::Zero;
	if (FSeinHoverMovementData* HoverData = MovementData.MovementClassData.GetMutablePtr<FSeinHoverMovementData>())
	{
		const FFixedPoint AltStep = HoverTuning.AltitudeChangeRate * DeltaTime;
		const FFixedPoint AltDelta = TargetAltitude - HoverData->Altitude;
		if (AltDelta > AltStep)        HoverData->Altitude = HoverData->Altitude + AltStep;
		else if (AltDelta < -AltStep)  HoverData->Altitude = HoverData->Altitude - AltStep;
		else                            HoverData->Altitude = TargetAltitude;
		CurrentAltitude = HoverData->Altitude;
	}

	// Z snap via QueryReferenceZ override -> GetCellHeightAt(false).
	// Z = surface top + altitude (sourced via GetAltitude → HoverData->Altitude).
	// Auto-clears whatever's in the cell.
	ApplyGroundSnapAndAltitude(NewPos, Ctx.MovementData, Nav, DeltaTime);

	UE_LOG(LogSeinHover, Verbose,
		TEXT("Hover: pos=(%.1f,%.1f,%.1f) alt=%.1f->%.1f speed=%.2f->%.2f distFinal=%.1f"),
		AgentPos.X.ToFloat(), AgentPos.Y.ToFloat(), AgentPos.Z.ToFloat(),
		CurrentAltitude.ToFloat(), TargetAltitude.ToFloat(),
		EntryMag.ToFloat() * (EntryDot >= FFixedPoint::Zero ? 1.0f : -1.0f), CurrentSpeed.ToFloat(),
		DistFinal.ToFloat());

	Entity.Transform.SetLocation(NewPos);
	// Persist velocity vector aligned with post-rotation forward.
	MovementData.Velocity = FFixedVector(CosY * CurrentSpeed, SinY * CurrentSpeed, FFixedPoint::Zero);

	return false;
}
