/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinCombatSubsystem.cpp
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @latest       23 Aug 2026
 * @brief        Implements the Combat target-index lifecycle.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "System/SeinCombatSubsystem.h"

#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

void USeinCombatSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UWorld* World = GetWorld();
	USeinWorldSubsystem* Sim =
		World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	if (Sim)
	{
		SimRef = Sim;
		Sim->OnAuthoritativeStateRestored.AddUObject(
			this, &USeinCombatSubsystem::InvalidateTargetIndex);
	}
}

void USeinCombatSubsystem::Deinitialize()
{
	ReleaseModuleOwnedState();
	Super::Deinitialize();
}

bool USeinCombatSubsystem::CollectTargetCandidates(
	const USeinWorldSubsystem& World,
	const FFixedVector& Origin,
	FFixedPoint Radius,
	FSeinEntityHandle Exclude,
	TArray<FSeinEntityHandle>& OutHandles) const
{
	return TargetIndex.QueryRadius(
		World, Origin, Radius, Exclude, OutHandles);
}

void USeinCombatSubsystem::InvalidateTargetIndex()
{
	TargetIndex.Invalidate();
}

void USeinCombatSubsystem::ReleaseModuleOwnedState()
{
	if (USeinWorldSubsystem* Sim = SimRef.Get())
	{
		Sim->OnAuthoritativeStateRestored.RemoveAll(this);
	}
	SimRef.Reset();
	TargetIndex.Invalidate();
}

void USeinCombatSubsystem::ReleaseModuleOwnedStateForModuleUnload()
{
	check(IsInGameThread());
	ReleaseModuleOwnedState();
	ReleaseHostedSystemsForModuleUnload();
}
