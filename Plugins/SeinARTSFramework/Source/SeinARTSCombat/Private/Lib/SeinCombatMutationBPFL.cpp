/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatMutationBPFL.cpp
 * @brief   Restricted combat mutation front doors — every call passes the
 *          sim-authorization gate before touching state.
 */

#include "Lib/SeinCombatMutationBPFL.h"
#include "Combat/SeinCombatDamage.h"
#include "Combat/SeinWeaponFire.h"
#include "Components/SeinVitalsComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/SeinVisualEvent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinCombatGameplayTags.h"

namespace
{
	USeinWorldSubsystem* GetAuthorizedSim(
		const UObject* WorldContextObject, const TCHAR* Operation)
	{
		UWorld* World = GEngine
			? GEngine->GetWorldFromContextObject(
				WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		USeinWorldSubsystem* Sim =
			World ? World->GetSubsystem<USeinWorldSubsystem>() : nullptr;
		if (!Sim || !Sim->RequireStateMutationAuthorization(Operation))
		{
			return nullptr;
		}
		return Sim;
	}
}

bool USeinCombatMutationBPFL::SeinFireWeaponAt(
	const UObject* WorldContextObject,
	FSeinEntityHandle Shooter,
	int32 WeaponIndex,
	FSeinEntityHandle Target)
{
	USeinWorldSubsystem* Sim =
		GetAuthorizedSim(WorldContextObject, TEXT("FireWeaponAt"));
	return Sim
		&& FSeinWeaponFire::TryFireWeaponAt(
			*Sim, Shooter, WeaponIndex, Target)
			== ESeinWeaponFireResult::Fired;
}

FFixedPoint USeinCombatMutationBPFL::SeinApplyDamage(
	const UObject* WorldContextObject,
	FSeinEntityHandle Target,
	FSeinEntityHandle Instigator,
	const FSeinDamagePayload& Payload)
{
	USeinWorldSubsystem* Sim =
		GetAuthorizedSim(WorldContextObject, TEXT("ApplyDamage"));
	if (!Sim)
	{
		return FFixedPoint::Zero;
	}
	return FSeinCombatDamage::ApplyDamage(
		*Sim, Target, Instigator, Payload);
}

FFixedPoint USeinCombatMutationBPFL::SeinApplyHeal(
	const UObject* WorldContextObject,
	FSeinEntityHandle Target,
	FSeinEntityHandle Instigator,
	FFixedPoint Amount)
{
	USeinWorldSubsystem* Sim =
		GetAuthorizedSim(WorldContextObject, TEXT("ApplyHeal"));
	if (!Sim || Amount <= FFixedPoint::Zero
		|| !Sim->IsEntityAlive(Target))
	{
		return FFixedPoint::Zero;
	}
	FSeinVitalsComponent* Vitals =
		Sim->GetComponentMutable<FSeinVitalsComponent>(Target);
	if (!Vitals || Vitals->Health <= FFixedPoint::Zero)
	{
		return FFixedPoint::Zero;
	}
	const FFixedPoint Headroom = Vitals->MaxHealth - Vitals->Health;
	const FFixedPoint Restored = Amount < Headroom ? Amount : Headroom;
	if (Restored > FFixedPoint::Zero)
	{
		Vitals->Health = Vitals->Health + Restored;
		Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeHealAppliedEvent(
			Target, Instigator, Restored,
			SeinCombatTags::Combat_Damage_Default));
	}
	return Restored;
}
