/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.h
 * @brief   World subsystem hook for registering the movement module's sim systems
 *          with the USeinWorldSubsystem tick loop on world begin-play, mirroring
 *          USeinSquadSubsystem's lifecycle. Currently registers NONE — the passive
 *          re-seek (FSeinPositionKeepSystem) was stripped 2026-06-03 pending a
 *          redesign after local avoidance lands; new systems register here.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinMovementSubsystem.generated.h"

UCLASS()
class SEINARTSMOVEMENT_API USeinMovementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
};
