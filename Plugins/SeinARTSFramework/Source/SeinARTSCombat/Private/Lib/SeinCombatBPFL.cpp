/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatBPFL.cpp
 * @brief   Read-side combat query implementations.
 */

#include "Lib/SeinCombatBPFL.h"
#include "Combat/SeinTargetQueryService.h"
#include "Combat/SeinWeaponFire.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	const USeinWorldSubsystem* GetSim(const UObject* WorldContextObject)
	{
		const UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(
				WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		return World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
	}
}

TArray<FSeinTargetCandidate> USeinCombatBPFL::SeinFindTargets(
	const UObject* WorldContextObject, const FSeinTargetQuery& Query)
{
	TArray<FSeinTargetCandidate> Candidates;
	if (const USeinWorldSubsystem* Sim = GetSim(WorldContextObject))
	{
		FSeinTargetQueryService::FindTargets(*Sim, Query, Candidates);
	}
	return Candidates;
}

FSeinVitalsComponent USeinCombatBPFL::SeinGetVitals(
	const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	bool& bFound)
{
	bFound = false;
	if (const USeinWorldSubsystem* Sim = GetSim(WorldContextObject))
	{
		if (const FSeinVitalsComponent* Vitals =
			Sim->GetComponent<FSeinVitalsComponent>(EntityHandle))
		{
			bFound = true;
			return *Vitals;
		}
	}
	return FSeinVitalsComponent();
}

bool USeinCombatBPFL::SeinIsWeaponReady(
	const UObject* WorldContextObject,
	FSeinEntityHandle EntityHandle,
	int32 WeaponIndex)
{
	const USeinWorldSubsystem* Sim = GetSim(WorldContextObject);
	return Sim
		&& FSeinWeaponFire::IsWeaponReady(*Sim, EntityHandle, WeaponIndex);
}
