/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWeaponCycleSystem.h
 * @brief   PreTick clockwork: weapon cooldown/reload timers, magazine
 *          seeding/refill, and vitals regeneration. Pure mechanism — never
 *          decides to fire, never picks targets.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinVitalsComponent.h"
#include "Components/SeinWeaponComponent.h"
#include "Core/SeinSystemPriority.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"

/**
 * System: Weapon Cycle
 * Phase: PreTick | Priority: 11 (after ability cooldowns)
 */
class FSeinWeaponCycleSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		// Gather-then-mutate: mutable component access must not run inside
		// the raw storage sweep (revision bookkeeping + reallocation rules).
		TArray<FSeinEntityHandle> ArmedHandles;
		if (const ISeinComponentStorage* WeaponStorage =
			World.GetComponentStorageRaw(FSeinWeaponComponent::StaticStruct()))
		{
			WeaponStorage->ForEachLiveComponent(
				[&](FSeinEntityHandle Handle, const void* /*Raw*/)
				{
					if (World.GetEntityPool().IsValid(Handle))
					{
						ArmedHandles.Add(Handle);
					}
				});
		}
		for (const FSeinEntityHandle& Handle : ArmedHandles)
		{
			FSeinWeaponComponent* Weapons =
				World.GetComponentMutable<FSeinWeaponComponent>(Handle);
			if (!Weapons) continue;
			if (!Weapons->bRuntimeSeeded)
			{
				for (FSeinWeaponSlot& Slot : Weapons->Weapons)
				{
					Slot.MagazineRemaining = Slot.MagazineSize;
				}
				Weapons->bRuntimeSeeded = true;
			}
			for (FSeinWeaponSlot& Slot : Weapons->Weapons)
			{
				if (Slot.CooldownRemaining > FFixedPoint::Zero)
				{
					Slot.CooldownRemaining =
						Slot.CooldownRemaining - DeltaTime;
					if (Slot.CooldownRemaining < FFixedPoint::Zero)
					{
						Slot.CooldownRemaining = FFixedPoint::Zero;
					}
				}
				if (Slot.ReloadRemaining > FFixedPoint::Zero)
				{
					Slot.ReloadRemaining = Slot.ReloadRemaining - DeltaTime;
					if (Slot.ReloadRemaining <= FFixedPoint::Zero)
					{
						Slot.ReloadRemaining = FFixedPoint::Zero;
						Slot.MagazineRemaining = Slot.MagazineSize;
					}
				}
			}
		}

		TArray<FSeinEntityHandle> RegenHandles;
		if (const ISeinComponentStorage* VitalsStorage =
			World.GetComponentStorageRaw(FSeinVitalsComponent::StaticStruct()))
		{
			VitalsStorage->ForEachLiveComponent(
				[&](FSeinEntityHandle Handle, const void* Raw)
				{
					const FSeinVitalsComponent* Vitals =
						static_cast<const FSeinVitalsComponent*>(Raw);
					if (World.GetEntityPool().IsValid(Handle)
						&& Vitals
						&& (Vitals->RegenPerSecond > FFixedPoint::Zero
							|| Vitals->Health <= FFixedPoint::Zero))
					{
						RegenHandles.Add(Handle);
					}
				});
		}
		for (const FSeinEntityHandle& Handle : RegenHandles)
		{
			FSeinVitalsComponent* Vitals =
				World.GetComponentMutable<FSeinVitalsComponent>(Handle);
			if (!Vitals) continue;
			// Authored-zero health seeds to MaxHealth on its first tick so
			// designers never have to mirror the two fields.
			if (Vitals->Health <= FFixedPoint::Zero)
			{
				Vitals->Health = Vitals->MaxHealth;
				continue;
			}
			if (Vitals->RegenPerSecond > FFixedPoint::Zero
				&& Vitals->Health < Vitals->MaxHealth)
			{
				Vitals->Health =
					Vitals->Health + Vitals->RegenPerSecond * DeltaTime;
				if (Vitals->Health > Vitals->MaxHealth)
				{
					Vitals->Health = Vitals->MaxHealth;
				}
			}
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.combat.weapon_cycle")),
			1u,
			ESeinTickPhase::PreTick,
			SeinSystemPriority::WeaponCycle);
	}
};
