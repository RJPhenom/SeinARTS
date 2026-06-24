/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.cpp
 * @brief   Registers the movement module's sim systems + hosts the persistent
 *          per-unit movement-instance registry (CP2.1, D-R2). See header.
 */

#include "SeinMovementSubsystem.h"
#include "Simulation/SeinAvoidanceSystem.h"
#include "Simulation/SeinMovementDriverSystem.h"
#include "Simulation/SeinNavContainmentSystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Movement/SeinMovement.h"
#include "Movement/SeinBasicMovement.h"
#include "Components/SeinMovementComponent.h"

void USeinMovementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Mirrors USeinSquadSubsystem's create + RegisterSystem lifecycle.
	USeinWorldSubsystem* Sim = InWorld.GetSubsystem<USeinWorldSubsystem>();
	if (!Sim) return;

	// Local avoidance — the soft steering layer above the penetration floor.
	AvoidanceSystem = new FSeinAvoidanceSystem();
	Sim->RegisterSystem(AvoidanceSystem);

	// The always-on per-unit movement driver (CP2.1, D-R2). Its first-contact
	// snap replaced FSeinInitialSnapSystem; its idle settle/coast is the
	// ground-up redesign the 2026-06-03 FSeinPositionKeepSystem strip was
	// deferred for (BAR semantics — settle in place, no return-to-home).
	DriverSystem = new FSeinMovementDriverSystem(this);
	Sim->RegisterSystem(DriverSystem);

	// Nav containment (PostTick 11) — keeps the nav-pure collision floor from
	// stranding units in baked walls / off the grid edge by pulling any
	// off-walkable movable collider back onto nav. Movement owns this (it may
	// know nav); the collision floor stays nav-agnostic.
	NavContainmentSystem = new FSeinNavContainmentSystem();
	Sim->RegisterSystem(NavContainmentSystem);
}

void USeinMovementSubsystem::Deinitialize()
{
	USeinWorldSubsystem* Sim = nullptr;
	if (UWorld* World = GetWorld())
	{
		Sim = World->GetSubsystem<USeinWorldSubsystem>();
	}

	if (NavContainmentSystem)
	{
		if (Sim) Sim->UnregisterSystem(NavContainmentSystem);
		delete NavContainmentSystem;
		NavContainmentSystem = nullptr;
	}
	if (DriverSystem)
	{
		if (Sim) Sim->UnregisterSystem(DriverSystem);
		delete DriverSystem;
		DriverSystem = nullptr;
	}
	if (AvoidanceSystem)
	{
		if (Sim) Sim->UnregisterSystem(AvoidanceSystem);
		delete AvoidanceSystem;
		AvoidanceSystem = nullptr;
	}

	MovementInstanceMap.Empty();
	MovementInstancePool.Empty();

	Super::Deinitialize();
}

UClass* USeinMovementSubsystem::ResolveMovementClass(const FSeinMovementComponent& Move)
{
	UClass* MoveClass = Move.MovementClass.IsValid()
		? Move.MovementClass.TryLoadClass<USeinMovement>()
		: nullptr;
	if (!MoveClass || MoveClass->HasAnyClassFlags(CLASS_Abstract))
	{
		MoveClass = USeinBasicMovement::StaticClass();
	}
	return MoveClass;
}

USeinMovement* USeinMovementSubsystem::GetOrCreateMovementInstance(
	FSeinEntityHandle Handle, const FSeinMovementComponent& Move)
{
	UClass* DesiredClass = ResolveMovementClass(Move);

	if (USeinMovement** Existing = MovementInstanceMap.Find(Handle))
	{
		if (*Existing && (*Existing)->GetClass() == DesiredClass)
		{
			return *Existing;
		}
		// Authored MovementClass changed at runtime (effect / designer swap) —
		// retire the old instance; the new mode starts with fresh kinematic
		// state (a different mode's steer/ramp state is meaningless to carry).
		MovementInstancePool.RemoveSingleSwap(*Existing);
		MovementInstanceMap.Remove(Handle);
	}

	USeinMovement* NewInstance = NewObject<USeinMovement>(this, DesiredClass);
	if (!NewInstance) return nullptr;

	// Hydrate per-unit tuning onto the fresh instance immediately, so any virtual that reads tuning
	// (GetAltitude / GetMinTurnRadius at plan-time, TickIdle, the steering hooks) sees correct values
	// from the very first use — not just after the first OnMoveBegin. No-op when there's no tuning.
	NewInstance->HydrateTuningFromData(Move.MovementClassData);

	MovementInstanceMap.Add(Handle, NewInstance);
	MovementInstancePool.Add(NewInstance);
	return NewInstance;
}

void USeinMovementSubsystem::SweepStaleMovementInstances(USeinWorldSubsystem& World)
{
	for (auto It = MovementInstanceMap.CreateIterator(); It; ++It)
	{
		if (World.GetEntityPool().IsValid(It->Key)) continue;
		MovementInstancePool.RemoveSingleSwap(It->Value);
		It.RemoveCurrent();
	}
}
