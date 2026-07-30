/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCooldownSystem.h
 * @brief   Ticks cooldowns on all ability instances each simulation frame.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinAbilityComponent.h"
#include "Abilities/SeinAbility.h"

/**
 * System: Cooldown Tick
 * Phase: PreTick | Priority: 10
 *
 * Iterates all entities with FSeinAbilityComponent and calls
 * TickCooldown(DeltaTime) on every ability instance, decrementing
 * cooldown timers towards zero.
 */
class FSeinCooldownSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, const FSeinEntity& /*Entity*/)
		{
			FSeinAbilityComponent* AbilityComp =
				World.GetComponentMutable<FSeinAbilityComponent>(
					Handle);
			if (!AbilityComp)
			{
				return;
			}

			for (int32 ID : AbilityComp->AbilityInstanceIDs)
			{
				if (USeinAbility* Ability = World.GetAbilityInstance(ID))
				{
					Ability->TickCooldown(DeltaTime);
				}
			}
		});
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.cooldown_tick")),
			1u,
			ESeinTickPhase::PreTick,
			SeinSystemPriority::CooldownTick);
	}
};
