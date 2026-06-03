/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.cpp
 * @brief   Registers the movement module's sim systems. See header.
 */

#include "SeinMovementSubsystem.h"

void USeinMovementSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// No movement sim systems are registered at present. The passive re-seek
	// (FSeinPositionKeepSystem) was stripped 2026-06-03 pending a ground-up
	// redesign after local avoidance lands. Future movement systems (avoidance,
	// re-seek v2) register here against the USeinWorldSubsystem tick loop,
	// mirroring USeinSquadSubsystem's create + RegisterSystem pattern.
}

void USeinMovementSubsystem::Deinitialize()
{
	Super::Deinitialize();
}
