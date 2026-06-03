/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchSettingsBPFL.cpp
 */

#include "Lib/SeinMatchSettingsBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

namespace
{
	const FSeinMatchSettings& ResolveLiveMatchSettings(const UObject* WorldContextObject)
	{
		static const FSeinMatchSettings Empty;
		if (!WorldContextObject) return Empty;
		const UWorld* World = WorldContextObject->GetWorld();
		if (!World) return Empty;
		const USeinWorldSubsystem* Sub = World->GetSubsystem<USeinWorldSubsystem>();
		return Sub ? Sub->GetMatchSettings() : Empty;
	}
}

FSeinMatchSettings USeinMatchSettingsBPFL::SeinGetMatchSettings(const UObject* WorldContextObject)
{
	return ResolveLiveMatchSettings(WorldContextObject);
}

TArray<FInstancedStruct> USeinMatchSettingsBPFL::SeinGetMatchExtensions(const UObject* WorldContextObject)
{
	return ResolveLiveMatchSettings(WorldContextObject).Extensions;
}
