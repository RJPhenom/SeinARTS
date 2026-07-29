/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEffectTickSystem.h
 * @brief   Ticks active effects: drains the pending-apply queue (DESIGN §8 Q9c),
 *          decrements finite durations, fires periodic OnTick hooks via CDO,
 *          and tears down expired effects (OnExpire + OnRemoved + tag ungrant).
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Core/SeinPlayerState.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Components/SeinActiveEffectsComponent.h"
#include "Effects/SeinActiveEffect.h"
#include "Effects/SeinEffect.h"

/**
 * System: Effect Tick
 * Phase: PreTick | Priority: 0
 *
 * Order of operations per tick:
 *   1. Drain the pending-apply queue (applies scheduled during last tick's hooks).
 *   2. Tick each active effect across all three scopes:
 *      - Timed (DurationMode == Timed): decrement RemainingDuration; mark expired if <= 0.
 *      - Persistent (DurationMode == Persistent): leave alone.
 *      - Periodic (TickInterval > 0): accumulate timer; fire OnTick on roll-over.
 *   3. Apply pending removals (expired durations → OnExpire + OnRemoved + tag ungrant).
 *
 * Only walks effects we can see from the World — the world subsystem's public
 * helpers handle the rest of the apply/remove machinery.
 */
class FSeinEffectTickSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		// Drain first, then snapshot every outer identity before a Blueprint hook
		// can grow component storage or rehash the player map.
		World.ProcessPendingEffectApplies();

		TArray<FSeinEntityHandle> EffectEntities;
		if (ISeinComponentStorage* Storage =
			World.GetComponentStorageRaw(FSeinActiveEffectsComponent::StaticStruct()))
		{
			FSeinEntityPool& Pool = World.GetEntityPool();
			Storage->ForEachLiveComponent([&](FSeinEntityHandle Handle, void* RawComponent)
			{
				const FSeinActiveEffectsComponent* EffectsComp = static_cast<const FSeinActiveEffectsComponent*>(RawComponent);
				if (EffectsComp && EffectsComp->ActiveEffects.Num() > 0
					&& Pool.IsValid(Handle))
				{
					EffectEntities.Add(Handle);
				}
			});
		}
		const TArray<FSeinPlayerID> PlayerIDs = World.GetRegisteredPlayerIDs();

		for (FSeinEntityHandle Entity : EffectEntities)
		{
			if (World.IsEntityAlive(Entity))
			{
				TickStorage(World, DeltaTime, Entity,
					FSeinPlayerID::Neutral(), ESeinModifierScope::Instance);
			}
		}

		// Canonical player order is gameplay-significant when hooks mutate shared
		// state or allocate globally ordered identities.
		for (FSeinPlayerID PlayerID : PlayerIDs)
		{
			TickStorage(World, DeltaTime, FSeinEntityHandle::Invalid(), PlayerID, ESeinModifierScope::Class);
			TickStorage(World, DeltaTime, FSeinEntityHandle::Invalid(), PlayerID, ESeinModifierScope::Player);
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.effect_tick")),
			1u,
			ESeinTickPhase::PreTick,
			SeinSystemPriority::EffectTick);
	}

private:
	static TArray<FSeinActiveEffect>* ResolveStorage(USeinWorldSubsystem& World,
		FSeinEntityHandle Entity, FSeinPlayerID PlayerID, ESeinModifierScope Scope)
	{
		if (Scope == ESeinModifierScope::Instance)
		{
			FSeinActiveEffectsComponent* Component = World.GetComponent<FSeinActiveEffectsComponent>(Entity);
			return Component ? &Component->ActiveEffects : nullptr;
		}
		FSeinPlayerState* State = World.GetPlayerState(PlayerID);
		if (!State)
		{
			return nullptr;
		}
		return Scope == ESeinModifierScope::Class ? &State->ClassEffects : &State->PlayerEffects;
	}

	static FSeinActiveEffect* FindEffect(TArray<FSeinActiveEffect>* Effects, int64 EffectID)
	{
		return Effects ? Effects->FindByPredicate([EffectID](const FSeinActiveEffect& Effect)
		{
			return Effect.EffectInstanceID == EffectID;
		}) : nullptr;
	}

	static void TickStorage(USeinWorldSubsystem& World, FFixedPoint DeltaTime,
		FSeinEntityHandle Entity, FSeinPlayerID PlayerID, ESeinModifierScope Scope)
	{
		const bool bInstanceScope = Scope == ESeinModifierScope::Instance;
		if (bInstanceScope && !World.IsEntityAlive(Entity))
		{
			return;
		}
		TArray<FSeinActiveEffect>* InitialStorage = ResolveStorage(World, Entity, PlayerID, Scope);
		if (!InitialStorage || InitialStorage->Num() == 0)
		{
			return;
		}

		TArray<int64, TInlineAllocator<8>> EffectIDs;
		EffectIDs.Reserve(InitialStorage->Num());
		for (const FSeinActiveEffect& Effect : *InitialStorage)
		{
			EffectIDs.Add(Effect.EffectInstanceID);
		}
		TArray<int64, TInlineAllocator<4>> ExpiredIDs;

		for (int64 EffectID : EffectIDs)
		{
			if (bInstanceScope && !World.IsEntityAlive(Entity))
			{
				return;
			}
			FSeinActiveEffect* Effect = FindEffect(ResolveStorage(World, Entity, PlayerID, Scope), EffectID);
			if (!Effect) continue;

			const USeinEffect* Def = Effect->EffectClass ? GetDefault<USeinEffect>(Effect->EffectClass) : nullptr;
			if (!Def) continue;

			if (Def->DurationMode == ESeinEffectDurationMode::Timed)
			{
				Effect->RemainingDuration = Effect->RemainingDuration - DeltaTime;
				if (Effect->RemainingDuration <= FFixedPoint::Zero)
				{
					ExpiredIDs.Add(EffectID);
					continue;
				}
			}

			if (Def->TickInterval > FFixedPoint::Zero)
			{
				Effect->TimeSinceLastPeriodic = Effect->TimeSinceLastPeriodic + DeltaTime;
				while (true)
				{
					if (bInstanceScope && !World.IsEntityAlive(Entity))
					{
						return;
					}
					Effect = FindEffect(ResolveStorage(World, Entity, PlayerID, Scope), EffectID);
					if (!Effect) break;
					Def = Effect->EffectClass ? GetDefault<USeinEffect>(Effect->EffectClass) : nullptr;
					if (!Def || Def->TickInterval <= FFixedPoint::Zero
						|| Effect->TimeSinceLastPeriodic < Def->TickInterval)
					{
						break;
					}

					const FFixedPoint Interval = Def->TickInterval;
					const FSeinEntityHandle CallbackTarget = Effect->Target;
					const TSubclassOf<USeinEffect> CallbackClass = Effect->EffectClass;
					Effect->TimeSinceLastPeriodic = Effect->TimeSinceLastPeriodic - Interval;
					const USeinEffect* CallbackDefinition = CallbackClass
						? GetDefault<USeinEffect>(CallbackClass) : nullptr;
					if (CallbackDefinition)
					{
						CallbackDefinition->OnTick(CallbackTarget, Interval);
					}
				}
			}
		}

		for (int64 ExpiredID : ExpiredIDs)
		{
			if (bInstanceScope)
			{
				if (!World.IsEntityAlive(Entity)) return;
				World.RemoveEffect(Entity, ExpiredID, /*bByExpiration=*/true);
			}
			else
			{
				World.RemovePlayerEffect(PlayerID, ExpiredID, /*bByExpiration=*/true);
			}
		}
	}
};
