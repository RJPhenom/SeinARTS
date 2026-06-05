/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.h
 * @brief   World subsystem hook for registering the movement module's sim systems
 *          with the USeinWorldSubsystem tick loop on world begin-play, mirroring
 *          USeinSquadSubsystem's lifecycle. Registers FSeinAvoidanceSystem (local
 *          unit-unit avoidance steering, PreTick) and FSeinInitialSnapSystem (one-time
 *          spawn floor-snap, PreTick). The passive re-seek (FSeinPositionKeepSystem)
 *          was stripped 2026-06-03 pending a redesign and is NOT re-added here.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinMovementSubsystem.generated.h"

class FSeinAvoidanceSystem;
class FSeinInitialSnapSystem;

UCLASS()
class SEINARTSMOVEMENT_API USeinMovementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	/** Local unit-unit avoidance steering system (PreTick). Owned here; registered
	 *  with the sim loop on world begin-play, unregistered + deleted on teardown. */
	FSeinAvoidanceSystem* AvoidanceSystem = nullptr;

	/** One-time spawn floor-snap system (PreTick) — snaps idle / never-moved units to
	 *  the ground (Z + slope pitch/roll) once, so a placed unit rests on the floor
	 *  before its first move order. Same ownership / lifecycle as AvoidanceSystem. */
	FSeinInitialSnapSystem* InitialSnapSystem = nullptr;
};
