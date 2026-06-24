/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlannerHandle.cpp
 */

#include "Movement/SeinPlannerHandle.h"
#include "Movement/SeinMovement.h"   // FSeinPlanPathContext, USeinMovement::ResolveCollisionRadius/GetMinTurnRadius
#include "SeinPathTypes.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Components/SeinMovementComponent.h"
#include "Components/SeinNavigationComponent.h"
#include "Types/Entity.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Settings/PluginSettings.h"

USeinMovement* USeinPlannerHandle::GetOwningMovement() const
{
	return GetTypedOuter<USeinMovement>();
}

bool USeinPlannerHandle::IsValidPlanner() const
{
	return Ctx != nullptr && OutPath != nullptr;
}

// ---- Planning inputs ----------------------------------------------------------

FFixedVector USeinPlannerHandle::GetStartLocation() const
{
	return Ctx ? Ctx->Entity.Transform.GetLocation() : FFixedVector::ZeroVector;
}

FFixedVector USeinPlannerHandle::GetDestination() const
{
	return Ctx ? Ctx->Destination : FFixedVector::ZeroVector;
}

FFixedPoint USeinPlannerHandle::GetFootprintRadius() const
{
	return Ctx ? USeinMovement::ResolveCollisionRadius(Ctx->World, Ctx->SelfHandle, Ctx->NavData) : FFixedPoint::Zero;
}

FFixedPoint USeinPlannerHandle::GetMinTurnRadius() const
{
	USeinMovement* Owner = GetOwningMovement();
	return (Owner && Ctx) ? Owner->GetMinTurnRadius(Ctx->MovementData) : FFixedPoint::Zero;
}

// ---- Build the output path ----------------------------------------------------

void USeinPlannerHandle::ClearPath()
{
	if (OutPath) OutPath->Clear();
}

void USeinPlannerHandle::AddWaypoint(const FFixedVector& Waypoint)
{
	if (OutPath) OutPath->Waypoints.Add(Waypoint);
}

void USeinPlannerHandle::FinalizePath(bool bIsPartial)
{
	if (!OutPath) return;
	OutPath->bIsValid = OutPath->Waypoints.Num() > 0;
	OutPath->bIsPartial = bIsPartial;
	OutPath->DeriveSegmentsFromWaypoints();
}

ESeinPathResult USeinPlannerHandle::BuildStraightLinePath()
{
	if (!Ctx || !OutPath) return ESeinPathResult::NoNavigation;
	OutPath->Clear();
	OutPath->Waypoints.Add(Ctx->Entity.Transform.GetLocation());
	OutPath->Waypoints.Add(Ctx->Destination);
	OutPath->bIsValid = true;
	OutPath->bIsPartial = false;
	OutPath->DeriveSegmentsFromWaypoints();
	return ESeinPathResult::Found;
}

ESeinPathResult USeinPlannerHandle::RequestNavPath()
{
	if (!Ctx || !OutPath || !Ctx->NavSub) return ESeinPathResult::NoNavigation;

	FSeinPathRequest Req;
	Req.Start     = Ctx->Entity.Transform.GetLocation();
	Req.End       = Ctx->Destination;
	Req.Requester = Ctx->SelfHandle;
	if (Ctx->NavData)
	{
		Req.AgentNavLayerMask     = Ctx->NavData->NavLayerMask;
		Req.AgentWallPaddingCells = Ctx->NavData->WallPadding;
		Req.AgentMaxSearchNodes   = Ctx->NavData->MaxSearchNodes;   // 0 = project default
	}
	// Footprint via the shared cascade (Extents -> NavComp -> 0), so the planner clears what the
	// body actually occupies — the same radius runtime collision uses.
	Req.AgentFootprintRadius = USeinMovement::ResolveCollisionRadius(Ctx->World, Ctx->SelfHandle, Ctx->NavData);
	// Authoritative destination (a cover slot overruling the coarse bake): honored as the exact
	// final waypoint even on a partial path. Unbound -> false (default nearest-reachable).
	Req.bAuthoritativeDestination = Ctx->World
		&& Ctx->World->AuthoritativeDestinationResolver.IsBound()
		&& Ctx->World->AuthoritativeDestinationResolver.Execute(Ctx->Destination);

	return Ctx->NavSub->RequestPath(Req, *OutPath);
}

// ---- Read the output path -----------------------------------------------------

int32 USeinPlannerHandle::GetPathWaypointCount() const
{
	return OutPath ? OutPath->Waypoints.Num() : 0;
}

FFixedVector USeinPlannerHandle::GetPathWaypoint(int32 Index) const
{
	return (OutPath && OutPath->Waypoints.IsValidIndex(Index)) ? OutPath->Waypoints[Index] : FFixedVector::ZeroVector;
}

// ---- Nav probes ---------------------------------------------------------------

bool USeinPlannerHandle::NavRaycast(const FFixedVector& From, const FFixedVector& To, FFixedVector& OutHitPoint) const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->NavRaycast(From, To, OutHitPoint) : false;
}

bool USeinPlannerHandle::SampleGroundHeight(const FFixedVector& WorldPos, bool bWalkableOnly, FFixedPoint& OutHeight) const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->GetCellHeightAt(WorldPos, OutHeight, bWalkableOnly) : false;
}

bool USeinPlannerHandle::ProjectToNav(const FFixedVector& WorldPos, FFixedVector& OutProjected) const
{
	OutProjected = WorldPos;
	return (Ctx && Ctx->Nav) ? Ctx->Nav->ProjectPointToNav(WorldPos, OutProjected) : false;
}

int32 USeinPlannerHandle::GetTerrainTypeAt(const FFixedVector& WorldPos) const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->GetTerrainTypeAt(WorldPos) : 0;
}

FGameplayTag USeinPlannerHandle::GetTerrainTagAt(const FFixedVector& WorldPos) const
{
	if (!Ctx || !Ctx->Nav) return FGameplayTag();
	return GetDefault<USeinARTSCoreSettings>()->GetTerrainTag(Ctx->Nav->GetTerrainTypeAt(WorldPos));
}

bool USeinPlannerHandle::IsPositionClear(const FFixedVector& WorldPos) const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->IsWorldPositionClear(WorldPos, /*AgentNavLayerMask*/ 0) : false;
}

FFixedPoint USeinPlannerHandle::GetCellSize() const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->GetCellSize() : FFixedPoint::Zero;
}
