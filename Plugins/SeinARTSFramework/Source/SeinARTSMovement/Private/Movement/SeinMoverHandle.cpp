/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMoverHandle.cpp
 *
 * Every node forwards a read/write to the borrowed context. The BP-facing names differ from the
 * underlying USeinMovement helper names by design (friendlier in graphs); the mapping is 1:1.
 */

#include "Movement/SeinMoverHandle.h"
#include "Movement/SeinMovement.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Types/Entity.h"
#include "Math/MathLib.h"
#include "SeinPathTypes.h"
#include "SeinNavigation.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Events/SeinVisualEvent.h"
#if UE_ENABLE_DEBUG_DRAWING
#include "DrawDebugHelpers.h"
#endif

USeinMovement* USeinMoverHandle::GetOwningMovement() const
{
	return GetTypedOuter<USeinMovement>();
}

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

FFixedPoint USeinMoverHandle::GetTurnRate() const
{
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->TurnRate : FFixedPoint::Zero;
}

// ---- Mode shape (per-class traits) --------------------------------------------

FFixedPoint USeinMoverHandle::GetMinTurnRadius() const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->GetMinTurnRadius(Ctx->MovementData) : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetFootprintRadius() const
{
	return Ctx ? USeinMovement::ResolveCollisionRadius(Ctx->World, Ctx->SelfHandle, Ctx->NavData) : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetAltitude() const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->GetAltitude(Ctx->MovementData) : FFixedPoint::Zero;
}

// ---- This tick's inputs -------------------------------------------------------

FFixedPoint USeinMoverHandle::GetDeltaTime() const
{
	return Ctx ? Ctx->DeltaTime : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetTerrainSpeedMultiplier() const
{
	return Ctx ? Ctx->TerrainSpeedMultiplier : FFixedPoint::One;
}

FFixedPoint USeinMoverHandle::GetAcceptanceRadius() const
{
	return Ctx ? SeinMath::Sqrt(Ctx->AcceptanceRadiusSq) : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::GetAcceptanceRadiusSquared() const
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
	if (!Ctx) return;
	const int32 Count = Ctx->Path.Waypoints.Num();
	Ctx->CurrentWaypointIndex = (Count > 0) ? FMath::Clamp(Index, 0, Count - 1) : 0;
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

FFixedPoint USeinMoverHandle::GetDistanceToFinalWaypoint() const
{
	if (!Ctx || Ctx->Path.Waypoints.Num() == 0) return FFixedPoint::Zero;
	FFixedVector ToFinal = Ctx->Path.Waypoints.Last() - GetLocation();
	ToFinal.Z = FFixedPoint::Zero;
	return ToFinal.Size();
}

int32 USeinMoverHandle::GetSegmentCount() const
{
	return Ctx ? Ctx->Path.Segments.Num() : 0;
}

FSeinPathSegment USeinMoverHandle::GetSegment(int32 Index) const
{
	if (!Ctx || !Ctx->Path.Segments.IsValidIndex(Index)) return FSeinPathSegment();
	return Ctx->Path.Segments[Index];
}

void USeinMoverHandle::EmitMovementCue(FGameplayTag CueTag, FFixedPoint Value) const
{
	if (!Ctx || !Ctx->World) return;
	Ctx->World->EnqueueVisualEvent(
		FSeinVisualEvent::MakeMovementCueEvent(Ctx->SelfHandle, CueTag, Value, GetLocation()));
}

void USeinMoverHandle::SetRenderValue(int32 Slot, FFixedPoint Value) const
{
	if (!Ctx || !Ctx->MovementData || Slot < 0 || Slot >= 64) return;  // 64-slot cap guards a runaway index
	TArray<FFixedPoint>& State = Ctx->MovementData->RenderState;
	if (Slot >= State.Num()) { State.SetNumZeroed(Slot + 1); }
	State[Slot] = Value;
}

// ---- Steering toolkit ---------------------------------------------------------

FFixedPoint USeinMoverHandle::GetEffectiveTopSpeed() const
{
	return Ctx ? USeinMovement::EffectiveTopSpeed(*Ctx) : FFixedPoint::Zero;
}

FFixedPoint USeinMoverHandle::StepSpeedToward(FFixedPoint Current, FFixedPoint Target, FFixedPoint Acceleration, FFixedPoint Deceleration) const
{
	return USeinMovement::StepSpeedToward(Current, Target, Acceleration, Deceleration, GetDeltaTime());
}

FFixedPoint USeinMoverHandle::GetArrivalSpeedCap(FFixedPoint DistanceToStop, FFixedPoint Deceleration) const
{
	return USeinMovement::KinematicArrivalSpeedCap(DistanceToStop, Deceleration);
}

FFixedPoint USeinMoverHandle::GetAdaptiveLookAheadDistance(FFixedPoint BaseDistance, FFixedPoint TimeHorizon, FFixedPoint AbsSpeed) const
{
	return USeinMovement::ComputeAdaptiveLookAhead(BaseDistance, TimeHorizon, AbsSpeed);
}

FFixedVector USeinMoverHandle::GetLookAheadPoint(FFixedPoint LookAhead) const
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

FFixedPoint USeinMoverHandle::GetAvoidanceSpeedScale() const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->GetAvoidanceSpeedScale(*Ctx) : FFixedPoint::One;
}

FFixedVector USeinMoverHandle::GetAvoidanceSteer() const
{
	// Raw 3D steer (incl. the Z channel an air model may write) straight off the per-tick
	// avoidance output — no planar projection, unlike ApplyAvoidanceSteer.
	return (Ctx && Ctx->MovementData) ? Ctx->MovementData->AvoidanceOutput.SteerDir : FFixedVector::ZeroVector;
}

FFixedVector USeinMoverHandle::ClampToNavigation(FFixedVector OldPos, FFixedVector NewPos) const
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

FFixedQuaternion USeinMoverHandle::ApplySlopeTilt(FFixedVector Pos, FFixedPoint Yaw) const
{
	USeinMovement* Owner = GetOwningMovement();
	if (!Owner || !Ctx || !Ctx->MovementData) return GetRotation();
	return Owner->ApplySlopeTilt(Pos, Yaw, Ctx->MovementData, Ctx->Nav, Ctx->DeltaTime);
}

bool USeinMoverHandle::GetDefaultReverseDecision(FFixedVector FinalGoal) const
{
	if (!Ctx || !Ctx->MovementData) return false;
	return USeinMovement::ShouldAutoReverse(GetLocation(), GetRotation(), FinalGoal, *Ctx->MovementData);
}

// ---- Navigation probes --------------------------------------------------------

bool USeinMoverHandle::NavRaycast(const FFixedVector& From, const FFixedVector& To, FFixedVector& OutHitPoint) const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->NavRaycast(From, To, OutHitPoint) : false;
}

bool USeinMoverHandle::SampleGroundHeight(const FFixedVector& WorldPos, bool bWalkableOnly, FFixedPoint& OutHeight) const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->GetCellHeightAt(WorldPos, OutHeight, bWalkableOnly) : false;
}

int32 USeinMoverHandle::GetTerrainTypeAt(const FFixedVector& WorldPos) const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->GetTerrainTypeAt(WorldPos) : 0;
}

FGameplayTag USeinMoverHandle::GetTerrainTagAt(const FFixedVector& WorldPos) const
{
	if (!Ctx || !Ctx->Nav) return FGameplayTag();
	return GetDefault<USeinARTSCoreSettings>()->GetTerrainTag(Ctx->Nav->GetTerrainTypeAt(WorldPos));
}

bool USeinMoverHandle::IsPositionClear(const FFixedVector& WorldPos) const
{
	// Mask MUST be non-zero: (BlockedNavLayerMask & 0) == 0 skips EVERY dynamic blocker,
	// silently degrading this to a static-only query that reports a dynamic-wall cell as
	// clear — the exact trap SeinNavigationBPFL documents. Use the agent's own nav layer
	// (0x01 ground fallback), matching the movement floor's CachedNavLayerMask.
	if (!Ctx || !Ctx->Nav) return false;
	const uint8 Mask = Ctx->NavData ? Ctx->NavData->NavLayerMask : uint8(0x01);
	return Ctx->Nav->IsWorldPositionClear(WorldPos, Mask);
}

FFixedVector USeinMoverHandle::QueryNavDirection(FFixedVector Goal, int64 GroupId) const
{
	if (!Ctx || !Ctx->Nav) return FFixedVector::ZeroVector;
	FSeinDirectionQuery Query;
	Query.From                = Ctx->Entity.Transform.GetLocation();
	Query.Goal                = Goal;
	Query.Requester           = Ctx->SelfHandle;
	Query.AgentFootprintRadius = GetFootprintRadius();
	Query.GroupId             = GroupId;
	return Ctx->Nav->QueryDirection(Query);
}

// ---- Debug draw (editor/development only; never mutates sim) -------------------

#if UE_ENABLE_DEBUG_DRAWING
namespace
{
	UWorld* MoverDebugWorld(const FSeinMovementContext* Ctx)
	{
		return (Ctx && Ctx->World) ? Ctx->World->GetWorld() : nullptr;
	}
	FVector MoverDebugVec(const FFixedVector& V)
	{
		return FVector(V.X.ToFloat(), V.Y.ToFloat(), V.Z.ToFloat());
	}
}
#endif

void USeinMoverHandle::DebugLine(const FFixedVector& Start, const FFixedVector& End, FLinearColor Color, FFixedPoint Thickness) const
{
#if UE_ENABLE_DEBUG_DRAWING
	if (UWorld* W = MoverDebugWorld(Ctx))
	{
		::DrawDebugLine(W, MoverDebugVec(Start), MoverDebugVec(End), Color.ToFColor(true),
			/*bPersistent*/ false, /*LifeTime*/ 0.0f, /*DepthPriority*/ 0, Thickness.ToFloat());
	}
#endif
}

void USeinMoverHandle::DebugArrow(const FFixedVector& Start, const FFixedVector& End, FLinearColor Color, FFixedPoint Thickness) const
{
#if UE_ENABLE_DEBUG_DRAWING
	if (UWorld* W = MoverDebugWorld(Ctx))
	{
		::DrawDebugDirectionalArrow(W, MoverDebugVec(Start), MoverDebugVec(End), /*ArrowSize*/ 20.0f,
			Color.ToFColor(true), /*bPersistent*/ false, /*LifeTime*/ 0.0f, /*DepthPriority*/ 0, Thickness.ToFloat());
	}
#endif
}

void USeinMoverHandle::DebugSphere(const FFixedVector& Center, FFixedPoint Radius, FLinearColor Color) const
{
#if UE_ENABLE_DEBUG_DRAWING
	if (UWorld* W = MoverDebugWorld(Ctx))
	{
		::DrawDebugSphere(W, MoverDebugVec(Center), Radius.ToFloat(), /*Segments*/ 16,
			Color.ToFColor(true), /*bPersistent*/ false, /*LifeTime*/ 0.0f, /*DepthPriority*/ 0, /*Thickness*/ 2.0f);
	}
#endif
}

void USeinMoverHandle::DebugCircle(const FFixedVector& Center, FFixedPoint Radius, FLinearColor Color) const
{
#if UE_ENABLE_DEBUG_DRAWING
	if (UWorld* W = MoverDebugWorld(Ctx))
	{
		// Flat on the ground (XY): explicit Y/Z axis pair, like the steering footprint ring.
		::DrawDebugCircle(W, MoverDebugVec(Center), Radius.ToFloat(), /*Segments*/ 32,
			Color.ToFColor(true), /*bPersistent*/ false, /*LifeTime*/ 0.0f, /*DepthPriority*/ 0, /*Thickness*/ 2.0f,
			/*YAxis*/ FVector(1, 0, 0), /*ZAxis*/ FVector(0, 1, 0), /*DrawAxis*/ false);
	}
#endif
}
