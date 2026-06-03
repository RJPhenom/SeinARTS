/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigation.cpp
 */

#include "SeinNavigation.h"
#include "SeinNavigationAsset.h"

TSubclassOf<USeinNavigationAsset> USeinNavigation::GetAssetClass() const
{
	return USeinNavigationAsset::StaticClass();
}

bool USeinNavigation::IsReachable(const FFixedVector& From, const FFixedVector& To, const FGameplayTagContainer& AgentTags) const
{
	FSeinPathRequest Request;
	Request.Start = From;
	Request.End = To;
	Request.BlockedTerrainTags = AgentTags;
	FSeinPath Path;
	return FindPath(Request, Path) && Path.bIsValid;
}

bool USeinNavigation::IsPlacementValid(const FFixedVector& CenterWorld, FFixedPoint /*YawDegrees*/,
	const FSeinExtentsShape& Shape, uint8 /*AgentLayerMask*/) const
{
	// Conservative reject if we have nothing to check against — better to say
	// "can't place" than "always allow" when nav is uninitialized.
	if (!HasRuntimeData()) return false;

	// DEFAULT IMPLEMENTATION CONTRACT:
	// This base implementation samples ONLY the center cell — a single
	// IsPassable call at CenterWorld + Shape.LocalOffset. It does NOT
	// rasterize the full footprint, does NOT account for rotation, and does
	// NOT consult AgentLayerMask. It is intentionally a deterministic scaffold:
	//   - Subclasses (USeinNavigationAStar and game-team replacements)
	//     SHOULD override with proper grid-AABB rasterization that walks the
	//     blocked-cell mask deterministically.
	//   - Float trig (sin/cos) over shape rotation is determinism-fragile
	//     across platforms — the override should use the same fixed-point
	//     primitives the rest of the nav uses.
	//   - The center-only check still catches "placed in middle of a wall" or
	//     "placed off the navmesh" — the easy cases — and lets game teams
	//     ship Phase 3 placement before the override lands.
	const FFixedVector SampleWorld(
		CenterWorld.X + Shape.LocalOffset.X,
		CenterWorld.Y + Shape.LocalOffset.Y,
		CenterWorld.Z);
	return IsPassable(SampleWorld);
}
