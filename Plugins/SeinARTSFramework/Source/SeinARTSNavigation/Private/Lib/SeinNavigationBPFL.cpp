/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationBPFL.cpp
 */

#include "Lib/SeinNavigationBPFL.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"

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
