/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSystemHostSubsystem.cpp
 * @brief   Managed register/unregister of hosted ISeinSystems. See header.
 */

#include "Simulation/SeinSystemHostSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"

void USeinSystemHostSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim)
	{
		return; // no sim in this world (e.g. a non-game world) — nothing to host
	}
	SimRef = Sim;

	CreateSystems(*Sim, HostedSystems);
	for (const TUniquePtr<ISeinSystem>& System : HostedSystems)
	{
		if (System)
		{
			Sim->RegisterSystem(System.Get());
		}
	}
}

void USeinSystemHostSubsystem::Deinitialize()
{
	if (USeinWorldSubsystem* Sim = SimRef.Get())
	{
		for (const TUniquePtr<ISeinSystem>& System : HostedSystems)
		{
			if (System)
			{
				Sim->UnregisterSystem(System.Get());
			}
		}
	}
	HostedSystems.Empty(); // TUniquePtr destroys each owned system
	SimRef.Reset();

	Super::Deinitialize();
}
