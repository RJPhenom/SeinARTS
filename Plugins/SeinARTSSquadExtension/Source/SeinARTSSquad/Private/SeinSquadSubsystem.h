/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadSubsystem.h
 * @brief   World subsystem that registers FSeinSquadSystem with the sim loop
 *          and maintains FormationWidth on squad brokers. Follows the same
 *          dynamic registration pattern as USeinNavigationSubsystem.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinSquadSubsystem.generated.h"

class ISeinSystem;

UCLASS()
class SEINARTSSQUAD_API USeinSquadSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	ISeinSystem* SquadSystem = nullptr;
};
