/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadSubsystem.cpp
 * @brief   Registers FSeinSquadSystem with the sim loop on world begin play.
 */

#include "SeinSquadSubsystem.h"
#include "SeinSquadSystem.h"
#include "Simulation/SeinWorldSubsystem.h"

void USeinSquadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	SquadSystem = new FSeinSquadSystem();
	Sim->RegisterSystem(SquadSystem);
}

void USeinSquadSubsystem::Deinitialize()
{
	if (SquadSystem)
	{
		if (UWorld* World = GetWorld())
		{
			if (USeinWorldSubsystem* Sim = World->GetSubsystem<USeinWorldSubsystem>())
			{
				Sim->UnregisterSystem(SquadSystem);
			}
		}
		delete SquadSystem;
		SquadSystem = nullptr;
	}
	Super::Deinitialize();
}
