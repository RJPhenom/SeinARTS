/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSystemHostSubsystem.cpp
 * @brief   Managed register/unregister of hosted ISeinSystems. See header.
 */

#include "Simulation/SeinSystemHostSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"

void USeinSystemHostSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency(USeinWorldSubsystem::StaticClass());

	UWorld* World = GetWorld();
	USeinWorldSubsystem* Sim =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
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
	ReleaseHostedSystems();

	Super::Deinitialize();
}

void USeinSystemHostSubsystem::ReleaseHostedSystemsForModuleUnload()
{
	check(IsInGameThread());
	ReleaseHostedSystems();
}

void USeinSystemHostSubsystem::ReleaseHostedSystems()
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
}
