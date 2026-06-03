/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementSubsystem.h
 * @brief   World subsystem that registers the movement module's sim systems with
 *          the USeinWorldSubsystem tick loop on world begin-play. Currently owns
 *          the passive re-seek (FSeinPositionKeepSystem). Mirrors
 *          USeinSquadSubsystem's lifecycle (create + RegisterSystem on begin-play,
 *          UnregisterSystem + delete on Deinitialize).
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinMovementSubsystem.generated.h"

class FSeinPositionKeepSystem;

UCLASS()
class SEINARTSMOVEMENT_API USeinMovementSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	/** Owned raw — the sim loop holds a non-owning ISeinSystem* to it. Deleted in
	 *  Deinitialize after unregistering. */
	FSeinPositionKeepSystem* PositionKeepSystem = nullptr;
};
