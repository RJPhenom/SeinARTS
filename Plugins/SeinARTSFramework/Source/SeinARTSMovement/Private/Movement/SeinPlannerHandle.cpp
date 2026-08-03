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
#include "Components/SeinBrokerMembershipData.h"
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

// ---- Building a TYPED path (arcs / drivable curves) ---------------------------

namespace
{
	// Push a segment's endpoints onto the coarse waypoint backbone: the first segment seeds its
	// From, every segment adds its To, so a chained sequence yields the corner polyline. This is
	// provisional — FlattenToWaypoints refines it into the drivable fine polyline at commit — but
	// it keeps Waypoints non-empty (the follower's Found gate) and coherent before then.
	void PushSegmentBackbone(FSeinPath* OutPath, const FFixedVector& From, const FFixedVector& To)
	{
		if (OutPath->Waypoints.Num() == 0) OutPath->Waypoints.Add(From);
		OutPath->Waypoints.Add(To);
	}
}

void USeinPlannerHandle::AddArcSegment(const FFixedVector& From, const FFixedVector& To,
	const FFixedVector& Center, FFixedPoint Radius, FFixedPoint Sweep, bool bReverse)
{
	if (!OutPath) return;
	FSeinPathSegment Seg;
	Seg.Type       = ESeinPathSegmentType::Arc;
	Seg.From       = From;
	Seg.To         = To;
	Seg.Center     = Center;
	Seg.Radius     = Radius;
	Seg.SweepAngle = Sweep;
	Seg.bReverse   = bReverse;
	OutPath->Segments.Add(MoveTemp(Seg));
	PushSegmentBackbone(OutPath, From, To);
}

void USeinPlannerHandle::AddStraightSegment(const FFixedVector& From, const FFixedVector& To, bool bReverse)
{
	if (!OutPath) return;
	FSeinPathSegment Seg;
	Seg.Type     = ESeinPathSegmentType::Straight;
	Seg.From     = From;
	Seg.To       = To;
	Seg.bReverse = bReverse;
	OutPath->Segments.Add(MoveTemp(Seg));
	PushSegmentBackbone(OutPath, From, To);
}

void USeinPlannerHandle::FinalizeTypedPath(bool bIsPartial)
{
	if (!OutPath) return;
	OutPath->bIsValid   = OutPath->Segments.Num() > 0 || OutPath->Waypoints.Num() > 0;
	OutPath->bIsPartial = bIsPartial;
	// Preserve the author-supplied typed Segments — do NOT call DeriveSegmentsFromWaypoints, which
	// would clobber them back to Straight (the clobber that made the read-only segment seam
	// unusable). Recompute TotalCost from true segment lengths: an arc contributes its arc length
	// (Radius * |Sweep|); every other kind its planar chord length.
	FFixedPoint Cost = FFixedPoint::Zero;
	for (const FSeinPathSegment& Seg : OutPath->Segments)
	{
		if (Seg.Type == ESeinPathSegmentType::Arc)
		{
			const FFixedPoint AbsSweep = (Seg.SweepAngle < FFixedPoint::Zero) ? -Seg.SweepAngle : Seg.SweepAngle;
			Cost += Seg.Radius * AbsSweep;
		}
		else
		{
			FFixedVector D = Seg.To - Seg.From;
			D.Z = FFixedPoint::Zero;
			Cost += D.Size();
		}
	}
	OutPath->TotalCost = Cost;
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
		Req.BlockedTerrainTags   = Ctx->NavData->BlockedTerrainTags;
		Req.AgentNavLayerMask     = Ctx->NavData->NavLayerMask;
		Req.AgentWallPaddingCells = Ctx->NavData->WallPadding;
		Req.AgentMaxSearchNodes   = Ctx->NavData->MaxSearchNodes;   // 0 = project default
	}
	// Footprint via the shared cascade (Extents -> NavComp -> 0), so the planner clears what the
	// body actually occupies — the same radius runtime collision uses.
	Req.AgentFootprintRadius = USeinMovement::ResolveCollisionRadius(Ctx->World, Ctx->SelfHandle, Ctx->NavData);
	// Group key for share-planning navs (flow field / hierarchical): the order's cohesion
	// group id, stamped on every member of a multi-element order. The shipped A* ignores
	// it; a shared-field nav keys one field/route per (GroupId, End). 0 = lone agent.
	if (Ctx->World)
	{
		if (const FSeinBrokerMembershipData* Membership =
				Ctx->World->GetComponent<FSeinBrokerMembershipData>(Ctx->SelfHandle))
		{
			Req.GroupId = Membership->CohesionGroupId;
		}
	}
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
	if (!Ctx || !Ctx->Nav) return false;
	FSeinNavAgentProfile Agent;
	Agent.Requester = Ctx->SelfHandle;
	Agent.AgentFootprintRadius = GetFootprintRadius();
	if (Ctx->NavData)
	{
		Agent.BlockedTerrainTags = Ctx->NavData->BlockedTerrainTags;
		Agent.AgentNavLayerMask = Ctx->NavData->NavLayerMask;
		Agent.AgentWallPaddingCells = Ctx->NavData->WallPadding;
	}
	return Ctx->Nav->ProjectPointToNavForAgent(
		WorldPos, Agent, OutProjected);
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
	if (!Ctx || !Ctx->Nav) return false;
	FSeinNavAgentProfile Agent;
	Agent.Requester = Ctx->SelfHandle;
	Agent.AgentFootprintRadius = GetFootprintRadius();
	if (Ctx->NavData)
	{
		Agent.BlockedTerrainTags = Ctx->NavData->BlockedTerrainTags;
		Agent.AgentNavLayerMask = Ctx->NavData->NavLayerMask;
		Agent.AgentWallPaddingCells = Ctx->NavData->WallPadding;
	}
	return Ctx->Nav->IsWorldPositionClearForAgent(
		WorldPos, Agent);
}

FFixedPoint USeinPlannerHandle::GetCellSize() const
{
	return (Ctx && Ctx->Nav) ? Ctx->Nav->GetCellSize() : FFixedPoint::Zero;
}
