/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatSubsystem.cpp
 * @brief   Hosts the weapon-cycle and projectile-flight systems.
 */

#include "System/SeinCombatSubsystem.h"
#include "System/SeinProjectileSystem.h"
#include "System/SeinWeaponCycleSystem.h"

void USeinCombatSubsystem::CreateSystems(
	USeinWorldSubsystem& /*Sim*/,
	TArray<TUniquePtr<ISeinSystem>>& OutSystems)
{
	OutSystems.Add(MakeUnique<FSeinWeaponCycleSystem>());
	OutSystems.Add(MakeUnique<FSeinProjectileSystem>());
}
