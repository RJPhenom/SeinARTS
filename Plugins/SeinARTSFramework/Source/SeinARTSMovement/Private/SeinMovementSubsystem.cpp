/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.cpp
 * @brief   Registers the movement module's sim systems. See header.
 */

#include "SeinMovementSubsystem.h"
#include "Simulation/SeinAvoidanceSystem.h"
#include "Simulation/SeinWorldSubsystem.h"

void USeinMovementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Local avoidance — the soft steering layer above the penetration floor. Mirrors
	// USeinSquadSubsystem's create + RegisterSystem lifecycle. (The passive re-seek
	// stripped 2026-06-03 is deliberately NOT re-added.)
	USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	AvoidanceSystem = new FSeinAvoidanceSystem();
	Sim->RegisterSystem(AvoidanceSystem);
}

void USeinMovementSubsystem::Deinitialize()
{
	if (AvoidanceSystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->UnregisterSystem(AvoidanceSystem);
			}
		}
		delete AvoidanceSystem;
		AvoidanceSystem = nullptr;
	}
	Super::Deinitialize();
}
