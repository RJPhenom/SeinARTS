/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverBPFL.cpp
 */

#include "Lib/SeinCoverBPFL.h"
#include "Lib/SeinCoverGeometry.h"
#include "System/SeinCoverSubsystem.h"
#include "System/SeinCoverSystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

TArray<FSeinCoverContext> USeinCoverBPFL::SeinQueryCoverAt(
	const UObject* WorldContextObject,
	FFixedVector WorldPoint)
{
	USeinCoverSystem* Cover = USeinCoverSubsystem::GetCoverSystemForWorld(WorldContextObject);
	if (!Cover) return {};
	return Cover->QueryCoverAt(WorldPoint);
}

FGameplayTag USeinCoverBPFL::SeinQueryBestCoverQualityAt(
	const UObject* WorldContextObject,
	FFixedVector WorldPoint)
{
	USeinCoverSystem* Cover = USeinCoverSubsystem::GetCoverSystemForWorld(WorldContextObject);
	if (!Cover) return FGameplayTag();
	return Cover->QueryBestCoverQualityAt(WorldPoint);
}

FFixedVector USeinCoverBPFL::SeinGetCoverDirection(
	const UObject* WorldContextObject,
	FFixedVector EntityWorldPosition,
	FSeinEntityHandle ProviderHandle)
{
	if (!WorldContextObject || !ProviderHandle.IsValid()) return FFixedVector::ZeroVector;
	UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull)
		: nullptr;
	if (!World) return FFixedVector::ZeroVector;
	USeinWorldSubsystem* WorldSub = World->GetSubsystem<USeinWorldSubsystem>();
	if (!WorldSub) return FFixedVector::ZeroVector;
	return SeinCoverGeometry::OutwardFromExtents(WorldSub, ProviderHandle, EntityWorldPosition);
}
