/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatMutationBPFL.cpp
 * @brief   Restricted combat notification front doors — every call passes
 *          the sim-authorization gate so presentation events stay ordered
 *          with the tick that produced them.
 */

#include "Lib/SeinCombatMutationBPFL.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Events/SeinVisualEvent.h"
#include "Simulation/SeinWorldSubsystem.h"

namespace
{
	/** Notifications follow the pool's live-only contract (tombstones of
	 *  entities destroyed earlier this tick are deliberately not readable by
	 *  ordinary lookups). A refused handle is therefore an ORDERING mistake
	 *  in the calling graph — Notify Death must precede Destroy Entity — so say
	 *  so instead of failing silently. */
	bool RequireLiveForNotification(
		const USeinWorldSubsystem& Sim, FSeinEntityHandle Handle,
		const TCHAR* Operation)
	{
		if (Sim.IsEntityAlive(Handle))
		{
			return true;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("%s: entity %s is not alive; call combat notifications BEFORE Destroy Entity."),
			Operation, *Handle.ToString());
		return false;
	}

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

bool USeinCombatMutationBPFL::SeinNotifyDamageApplied(
	const UObject* WorldContextObject,
	FSeinEntityHandle Target,
	FSeinEntityHandle Source,
	FFixedPoint Amount,
	FGameplayTag DamageType)
{
	USeinWorldSubsystem* Sim =
		GetAuthorizedSim(WorldContextObject, TEXT("NotifyDamageApplied"));
	if (!Sim || !RequireLiveForNotification(
			*Sim, Target, TEXT("NotifyDamageApplied")))
	{
		return false;
	}
	Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeDamageAppliedEvent(
		Target, Source, Amount, DamageType));
	return true;
}

bool USeinCombatMutationBPFL::SeinNotifyHealApplied(
	const UObject* WorldContextObject,
	FSeinEntityHandle Target,
	FSeinEntityHandle Source,
	FFixedPoint Amount,
	FGameplayTag HealType)
{
	USeinWorldSubsystem* Sim =
		GetAuthorizedSim(WorldContextObject, TEXT("NotifyHealApplied"));
	if (!Sim || !RequireLiveForNotification(
			*Sim, Target, TEXT("NotifyHealApplied")))
	{
		return false;
	}
	Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeHealAppliedEvent(
		Target, Source, Amount, HealType));
	return true;
}

bool USeinCombatMutationBPFL::SeinNotifyDeath(
	const UObject* WorldContextObject,
	FSeinEntityHandle Dying,
	FSeinEntityHandle Killer)
{
	USeinWorldSubsystem* Sim =
		GetAuthorizedSim(WorldContextObject, TEXT("NotifyDeath"));
	if (!Sim || !RequireLiveForNotification(*Sim, Dying, TEXT("NotifyDeath")))
	{
		return false;
	}
	Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeDeathEvent(Dying, Killer));
	// Kill attribution needs a live killer (owner lookups are live-only);
	// an invalid or already-destroyed killer simply yields no kill-feed entry.
	if (Sim->IsEntityAlive(Killer))
	{
		Sim->EnqueueVisualEvent(FSeinVisualEvent::MakeKillEvent(
			Killer, Dying, Sim->GetEntityOwner(Killer)));
	}
	return true;
}
