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
	return Nav->IsWorldPositionClear(WorldPos, /*AgentNavLayerMask*/ 0);
}

FFixedPoint USeinNavigationBPFL::SeinGetCellSize(const UObject* WorldContextObject)
{
	USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(WorldContextObject);
	return Nav ? Nav->GetCellSize() : FFixedPoint::Zero;
}
