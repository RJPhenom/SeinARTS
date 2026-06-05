/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.cpp
 * @brief   Registers the movement module's sim systems. See header.
 */

#include "SeinMovementSubsystem.h"
#include "Simulation/SeinAvoidanceSystem.h"
#include "Simulation/SeinInitialSnapSystem.h"
#include "Simulation/SeinWorldSubsystem.h"

void USeinMovementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Mirrors USeinSquadSubsystem's create + RegisterSystem lifecycle. (The passive
	// re-seek stripped 2026-06-03 is deliberately NOT re-added.)
	USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	// Local avoidance — the soft steering layer above the penetration floor.
	AvoidanceSystem = new FSeinAvoidanceSystem();
	Sim->RegisterSystem(AvoidanceSystem);

	// One-time spawn floor-snap — places idle / never-moved units on the ground
	// (Z + slope pitch/roll) before their first move order.
	InitialSnapSystem = new FSeinInitialSnapSystem();
	Sim->RegisterSystem(InitialSnapSystem);
}

void USeinMovementSubsystem::Deinitialize()
{
	USeinWorldSubsystem* Sim = nullptr;
	if (UWorld* World = GetWorld())
	{
		Sim = World->GetSubsystem<USeinWorldSubsystem>();
	}

	if (InitialSnapSystem)
	{
		if (Sim) Sim->UnregisterSystem(InitialSnapSystem);
		delete InitialSnapSystem;
		InitialSnapSystem = nullptr;
	}
	if (AvoidanceSystem)
	{
		if (Sim) Sim->UnregisterSystem(AvoidanceSystem);
		delete AvoidanceSystem;
		AvoidanceSystem = nullptr;
	}
	Super::Deinitialize();
}
