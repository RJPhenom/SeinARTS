/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.cpp
 * @brief   Registers the movement module's sim systems. See header.
 */

#include "SeinMovementSubsystem.h"
#include "Simulation/SeinPositionKeepSystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Engine/World.h"

void USeinMovementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) { return; }

	PositionKeepSystem = new FSeinPositionKeepSystem();
	Sim->RegisterSystem(PositionKeepSystem);
}

void USeinMovementSubsystem::Deinitialize()
{
	if (PositionKeepSystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->UnregisterSystem(PositionKeepSystem);
			}
		}
		delete PositionKeepSystem;
		PositionKeepSystem = nullptr;
	}

	Super::Deinitialize();
}
