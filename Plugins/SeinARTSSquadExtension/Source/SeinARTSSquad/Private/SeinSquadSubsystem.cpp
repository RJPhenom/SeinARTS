/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadSubsystem.cpp
 * @brief   Hosts FSeinSquadSystem via the managed USeinSystemHostSubsystem base.
 */

#include "SeinSquadSubsystem.h"
#include "SeinSquadSystem.h"

void USeinSquadSubsystem::CreateSystems(USeinWorldSubsystem& /*Sim*/, TArray<TUniquePtr<ISeinSystem>>& OutSystems)
{
	OutSystems.Add(MakeUnique<FSeinSquadSystem>());
}
