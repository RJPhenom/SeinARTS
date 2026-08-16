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
#include "Events/SeinVisualEvent.h"
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
		// Gather-then-mutate, and gather ONLY handles that will actually
		// change: a mutable fetch touches the slot's mutation revision, so an
		// unconditional per-entity fetch would mark every armed/vitals entity
		// dirty every tick and defeat the incremental canonical root's
		// compare-on-write savings.
		TArray<FSeinEntityHandle> ArmedHandles;
		if (const ISeinComponentStorage* WeaponStorage =
			World.GetComponentStorageRaw(FSeinWeaponComponent::StaticStruct()))
		{
			WeaponStorage->ForEachLiveComponent(
				[&](FSeinEntityHandle Handle, const void* Raw)
				{
					const FSeinWeaponComponent* Weapons =
						static_cast<const FSeinWeaponComponent*>(Raw);
					if (!Weapons || !World.GetEntityPool().IsValid(Handle))
					{
						return;
					}
					bool bNeedsMutation = !Weapons->bRuntimeSeeded;
					for (const FSeinWeaponSlot& Slot : Weapons->Weapons)
					{
						bNeedsMutation = bNeedsMutation
							|| Slot.CooldownRemaining > FFixedPoint::Zero
							|| Slot.ReloadRemaining > FFixedPoint::Zero;
					}
					if (bNeedsMutation)
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

		TArray<FSeinEntityHandle> VitalsHandles;
		if (const ISeinComponentStorage* VitalsStorage =
			World.GetComponentStorageRaw(FSeinVitalsComponent::StaticStruct()))
		{
			VitalsStorage->ForEachLiveComponent(
				[&](FSeinEntityHandle Handle, const void* Raw)
				{
					const FSeinVitalsComponent* Vitals =
						static_cast<const FSeinVitalsComponent*>(Raw);
					if (!Vitals || !World.GetEntityPool().IsValid(Handle))
					{
						return;
					}
					const bool bNeedsSeed = !Vitals->bHealthSeeded;
					const bool bZeroed =
						Vitals->Health <= FFixedPoint::Zero;
					const bool bRegens =
						Vitals->RegenPerSecond > FFixedPoint::Zero
						&& Vitals->Health < Vitals->MaxHealth;
					if (bNeedsSeed || bZeroed || bRegens)
					{
						VitalsHandles.Add(Handle);
					}
				});
		}
		for (const FSeinEntityHandle& Handle : VitalsHandles)
		{
			FSeinVitalsComponent* Vitals =
				World.GetComponentMutable<FSeinVitalsComponent>(Handle);
			if (!Vitals) continue;
			// One-time seed: authored-zero health fills to MaxHealth so
			// designers never mirror the two fields.
			if (!Vitals->bHealthSeeded)
			{
				if (Vitals->Health <= FFixedPoint::Zero)
				{
					Vitals->Health = Vitals->MaxHealth;
				}
				Vitals->bHealthSeeded = true;
			}
			// After the seed, zero ALWAYS means death — a scripted
			// whole-struct write that zeroed health (bypassing the damage
			// path) still gets a real death, never a silent re-seed revive.
			// This also retires misauthored MaxHealth <= 0 entities instead
			// of re-gathering them forever.
			if (Vitals->Health <= FFixedPoint::Zero)
			{
				World.EnqueueVisualEvent(FSeinVisualEvent::MakeDeathEvent(
					Handle, FSeinEntityHandle()));
				World.DestroyEntity(Handle);
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
