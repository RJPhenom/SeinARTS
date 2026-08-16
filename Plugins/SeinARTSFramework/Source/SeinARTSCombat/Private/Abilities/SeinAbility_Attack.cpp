/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbility_Attack.cpp
 * @brief   Starter attack: gate on an armed owner + damageable target, then
 *          fire whatever is legal each tick until the target is gone.
 */

#include "Abilities/SeinAbility_Attack.h"
#include "Combat/SeinWeaponFire.h"
#include "Components/SeinVitalsComponent.h"
#include "Components/SeinWeaponComponent.h"
#include "Simulation/SeinWorldSubsystem.h"

USeinAbility_Attack::USeinAbility_Attack()
{
	AbilityName = FText::FromString(TEXT("Attack"));
	TargetType = ESeinAbilityTargetType::Entity;
}

bool USeinAbility_Attack::CanActivate_Implementation()
{
	// CanActivate runs BEFORE the pipeline assigns this activation's target
	// onto the instance — TargetEntity here is stale state from a previous
	// activation, never the pending command's target. Gate only on what is
	// knowable now (an armed owner); the command pipeline's declarative
	// target validation owns the target, and the first OnTick ends cleanly
	// if it is gone or undamageable by then.
	if (!WorldSubsystem) return false;
	const FSeinWeaponComponent* Weapons =
		WorldSubsystem->GetComponent<FSeinWeaponComponent>(OwnerEntity);
	return Weapons && Weapons->Weapons.Num() > 0;
}

void USeinAbility_Attack::OnTick_Implementation(FFixedPoint DeltaTime)
{
	if (!WorldSubsystem) { EndAbility(); return; }
	if (!WorldSubsystem->IsEntityAlive(TargetEntity))
	{
		EndAbility();
		return;
	}
	const FSeinWeaponComponent* Weapons =
		WorldSubsystem->GetComponent<FSeinWeaponComponent>(OwnerEntity);
	if (!Weapons || Weapons->Weapons.Num() == 0)
	{
		EndAbility();
		return;
	}
	const int32 WeaponCount = Weapons->Weapons.Num();
	for (int32 Index = 0; Index < WeaponCount; ++Index)
	{
		// Refusals (cooling down, out of range, no LoS) are normal — the
		// starter simply holds fire on those slots this tick.
		FSeinWeaponFire::TryFireWeaponAt(
			*WorldSubsystem, OwnerEntity, Index, TargetEntity);
		// Firing can reallocate storage or even destroy the target; the
		// fire gate re-validates everything per slot, so no re-fetch here.
	}
}
