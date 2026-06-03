/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPlayerBPFL.cpp
 * @brief   Implementation of per-player entity query nodes.
 */

#include "Lib/SeinPlayerBPFL.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinEntityPool.h"

USeinWorldSubsystem* USeinPlayerBPFL::GetWorldSubsystem(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
}

TArray<FSeinEntityHandle> USeinPlayerBPFL::SeinGetPlayerEntities(const UObject* WorldContextObject, FSeinPlayerID PlayerID)
{
	TArray<FSeinEntityHandle> Result;
	USeinWorldSubsystem* Subsystem = GetWorldSubsystem(WorldContextObject);
	if (!Subsystem) return Result;

	const FSeinEntityPool& Pool = Subsystem->GetEntityPool();
	Pool.ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
	{
		if (Subsystem->GetEntityOwner(Handle) == PlayerID)
		{
			Result.Add(Handle);
		}
	});

	return Result;
}
