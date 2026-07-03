/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationBPFL.cpp
 */

#include "Lib/SeinNavigationBPFL.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Settings/PluginSettings.h"
#include "Types/Random.h"

FSeinPath USeinNavigationBPFL::SeinFindPath(const UObject* WorldContextObject, FFixedVector Start, FFixedVector End, FSeinEntityHandle Requester, FGameplayTagContainer BlockedTerrainTags)
{
	FSeinPath Result;
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return Result;

	FSeinPathRequest Req;
	Req.Start = Start;
	Req.End = End;
	Req.Requester = Requester;
	Req.BlockedTerrainTags = BlockedTerrainTags;
	Nav->FindPath(Req, Result);
	return Result;
}

FFixedVector USeinNavigationBPFL::SeinQueryNavDirection(const UObject* WorldContextObject, FFixedVector From, FFixedVector Goal, FSeinEntityHandle Requester, FGameplayTagContainer BlockedTerrainTags, int64 GroupId)
{
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return FFixedVector::ZeroVector;

	FSeinDirectionQuery Query;
	Query.From = From;
	Query.Goal = Goal;
	Query.Requester = Requester;
	Query.BlockedTerrainTags = BlockedTerrainTags;
	Query.GroupId = GroupId;
	return Nav->QueryDirection(Query);
}

bool USeinNavigationBPFL::SeinIsLocationReachable(const UObject* WorldContextObject, FFixedVector From, FFixedVector To, FGameplayTagContainer AgentTags)
{
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return false;
	return Nav->IsReachable(From, To, AgentTags);
}

bool USeinNavigationBPFL::SeinGetRandomReachablePoint(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, int64 Seed, FFixedVector& OutPoint)
{
	OutPoint = Origin;
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return false;
	FFixedRandom Rng(static_cast<uint64>(Seed));
	return Nav->GetRandomReachablePoint(Origin, Radius, Rng, OutPoint);
}

bool USeinNavigationBPFL::SeinNavRaycast(const UObject* WorldContextObject, FFixedVector From, FFixedVector To, FFixedVector& OutHitPoint)
{
	OutHitPoint = To;
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return false;
	return Nav->NavRaycast(From, To, OutHitPoint);
}

bool USeinNavigationBPFL::SeinGetCellHeightAt(const UObject* WorldContextObject, FFixedVector WorldPos, bool bWalkableOnly, FFixedPoint& OutHeight)
{
	OutHeight = WorldPos.Z;
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return false;
	return Nav->GetCellHeightAt(WorldPos, OutHeight, bWalkableOnly);
}

int32 USeinNavigationBPFL::SeinGetTerrainTypeAt(const UObject* WorldContextObject, FFixedVector WorldPos)
{
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	return Nav ? Nav->GetTerrainTypeAt(WorldPos) : 0;
}

FGameplayTag USeinNavigationBPFL::SeinGetTerrainTagAt(const UObject* WorldContextObject, FFixedVector WorldPos)
{
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return FGameplayTag();
	return GetDefault<USeinARTSCoreSettings>()->GetTerrainTag(Nav->GetTerrainTypeAt(WorldPos));
}

bool USeinNavigationBPFL::SeinIsPositionClear(const UObject* WorldContextObject, FFixedVector WorldPos)
{
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	if (!Nav) return false;
	// Match-all layer mask (0xFF) so EVERY dynamic blocker is considered, consistent
	// with FSeinPathRequest's default mask. Mask 0 would AND to zero against every
	// blocker's BlockedNavLayerMask and skip them all, wrongly reporting blocked
	// cells as clear. This no-arg BP node has no agent context, so match-all is the
	// correct conservative default.
	return Nav->IsWorldPositionClear(WorldPos, /*AgentNavLayerMask*/ 0xFF);
}

FFixedPoint USeinNavigationBPFL::SeinGetCellSize(const UObject* WorldContextObject)
{
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	return Nav ? Nav->GetCellSize() : FFixedPoint::Zero;
}
