/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSquadSubsystem.h
 * @brief   World subsystem that hosts FSeinSquadSystem on the sim loop. Uses the
 *          managed USeinSystemHostSubsystem base — registration + lifetime are
 *          handled by the base; this just declares which system(s) to host.
 */

#pragma once

#include "CoreMinimal.h"
#include "Simulation/SeinSystemHostSubsystem.h"
#include "SeinSquadSubsystem.generated.h"

class ISeinSystem;
class USeinWorldSubsystem;

UCLASS()
class SEINARTSSQUAD_API USeinSquadSubsystem : public USeinSystemHostSubsystem
{
	GENERATED_BODY()

protected:
	virtual void CreateSystems(USeinWorldSubsystem& Sim, TArray<TUniquePtr<ISeinSystem>>& OutSystems) override;
};
