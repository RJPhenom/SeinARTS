/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityTickSystem.h
 * @brief   Ticks all active abilities (primary and passives) each simulation frame.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinAbilityComponent.h"
#include "Abilities/SeinAbility.h"

/**
 * System: Ability Tick
 * Phase: AbilityExecution | Priority: 0
 *
 * Iterates all entities with FSeinAbilityComponent. For each entity,
 * ticks the active primary ability and all active passive abilities
 * by calling TickAbility(DeltaTime).
 */
class FSeinAbilityTickSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
		{
			FSeinAbilityComponent* AbilityComp =
				World.GetComponentMutable<FSeinAbilityComponent>(
					Handle);
			if (!AbilityComp)
			{
				return;
			}

			// Tick primary active ability.
			USeinAbility* Active = AbilityComp->GetActiveAbility(World);
			if (Active && Active->bIsActive)
			{
				Active->TickAbility(DeltaTime);
			}

			// Tick all active passive abilities.
			for (int32 ID : AbilityComp->ActivePassiveIDs)
			{
				if (USeinAbility* Passive = World.GetAbilityInstance(ID))
				{
					if (Passive->bIsActive)
					{
						Passive->TickAbility(DeltaTime);
					}
				}
			}
		});
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.ability_tick")),
			1u,
			ESeinTickPhase::AbilityExecution,
			SeinSystemPriority::AbilityTick);
	}
};
