/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatSubsystem.h
 * @brief   World subsystem hosting the combat clockwork systems on the sim
 *          loop via the managed USeinSystemHostSubsystem base.
 */

#pragma once

#include "CoreMinimal.h"
#include "Simulation/SeinSystemHostSubsystem.h"
#include "SeinCombatSubsystem.generated.h"

class ISeinSystem;
class USeinWorldSubsystem;

UCLASS()
class SEINARTSCOMBAT_API USeinCombatSubsystem : public USeinSystemHostSubsystem
{
	GENERATED_BODY()

protected:
	virtual void CreateSystems(
		USeinWorldSubsystem& Sim,
		TArray<TUniquePtr<ISeinSystem>>& OutSystems) override;
};
