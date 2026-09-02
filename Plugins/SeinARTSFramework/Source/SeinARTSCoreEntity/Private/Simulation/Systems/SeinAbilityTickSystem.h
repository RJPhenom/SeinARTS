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
#include "Simulation/ComponentStorage.h"
#include "Components/SeinAbilityPayload.h"
#include "Abilities/SeinAbility.h"

/**
 * System: Ability Tick
 * Phase: AbilityExecution | Priority: 0
 *
 * Iterates all entities with FSeinAbilityPayload. For each entity,
 * ticks the active primary ability and all active passive abilities
 * by calling TickAbility(DeltaTime).
 */
class FSeinAbilityTickSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		const ISeinComponentStorage* Storage =
			World.GetComponentStorageRaw(FSeinAbilityPayload::StaticStruct());
		if (!Storage) return;

		// Ability callbacks may alter component storage. Snapshot the sparse,
		// ascending handle list, then reacquire each component before use.
		AbilityHandles.Reset();
		AbilityHandles.Reserve(Storage->GetComponentCount());
		Storage->ForEachLiveComponent([this](
			FSeinEntityHandle Handle, const void* /*RawComponent*/)
		{
			AbilityHandles.Add(Handle);
		});
		for (const FSeinEntityHandle Handle : AbilityHandles)
		{
			if (!World.GetEntityPool().IsValid(Handle)) continue;
			const FSeinAbilityPayload* AbilityComp =
				World.GetComponent<FSeinAbilityPayload>(
					Handle);
			if (!AbilityComp)
			{
				continue;
			}

			// Tick primary active ability.
			USeinAbility* Active = AbilityComp->GetActiveAbility(World);
			if (Active && Active->bIsActive)
			{
				Active->TickAbility(DeltaTime);
			}

			// A primary callback can grow or alter component storage. Reacquire,
			// then snapshot passive IDs so no callback leaves a stale array view.
			AbilityComp = World.GetComponent<FSeinAbilityPayload>(Handle);
			if (!AbilityComp) continue;
			TArray<int32, TInlineAllocator<8>> PassiveIDs;
			PassiveIDs.Append(AbilityComp->ActivePassiveIDs);
			for (int32 ID : PassiveIDs)
			{
				if (USeinAbility* Passive = World.GetAbilityInstance(ID))
				{
					if (Passive->bIsActive)
					{
						Passive->TickAbility(DeltaTime);
					}
				}
			}
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.ability_tick")),
			2u,
			ESeinTickPhase::AbilityExecution,
			SeinSystemPriority::AbilityTick);
	}

private:
	TArray<FSeinEntityHandle> AbilityHandles;
};
