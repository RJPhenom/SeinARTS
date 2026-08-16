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
	if (!WorldSubsystem) return false;
	const FSeinWeaponComponent* Weapons =
		WorldSubsystem->GetComponent<FSeinWeaponComponent>(OwnerEntity);
	return Weapons && Weapons->Weapons.Num() > 0
		&& WorldSubsystem->IsEntityAlive(TargetEntity)
		&& WorldSubsystem->GetComponent<FSeinVitalsComponent>(TargetEntity)
			!= nullptr;
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
