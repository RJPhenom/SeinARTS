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
#include "Core/SeinPlayerState.h"
#include "Simulation/SeinWorldSubsystem.h"
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
		// 1. Drain pending-apply queue first so newly-applied effects participate
		// in this tick's duration/interval math.
		World.ProcessPendingEffectApplies();

		// 2. Tick per-entity Instance-scope effects.
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
		{
			FSeinActiveEffectsComponent* EffectsComp = World.GetComponent<FSeinActiveEffectsComponent>(Handle);
			if (!EffectsComp || EffectsComp->ActiveEffects.Num() == 0) return;

			TickEffectsArray(Handle, EffectsComp->ActiveEffects, DeltaTime, World);
		});

		// 3. Tick per-player Class + Player scope effects. Class/Player-scope
		// ticks don't have a natural single "target entity" — we use the stored Target
		// on each FSeinActiveEffect. That target may have been destroyed; OnTick/OnExpire
		// hooks receive a potentially-stale handle which BP authors must validate.
		World.ForEachPlayerStateMutable([&](FSeinPlayerID /*PlayerID*/, FSeinPlayerState& State)
		{
			TickPlayerScopedArray(State.ClassEffects, DeltaTime, World);
			TickPlayerScopedArray(State.PlayerEffects, DeltaTime, World);
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 0; }
	virtual FName GetSystemName() const override { return TEXT("EffectTick"); }

private:
	/** Tick a per-entity effects array (Instance scope); remove-by-expiration routes
	 *  through World.RemoveInstanceEffect so tag ungrant + hooks + visual events fire. */
	static void TickEffectsArray(FSeinEntityHandle Handle, TArray<FSeinActiveEffect>& Effects, FFixedPoint DeltaTime, USeinWorldSubsystem& World)
	{
		TArray<uint32> Expired;

		for (FSeinActiveEffect& Effect : Effects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) { continue; }

			// Finite duration — decrement. Zero = instant (shouldn't be in storage).
			// Negative = infinite (leave alone).
			if (Def->DurationMode == ESeinEffectDurationMode::Timed)
			{
				Effect.RemainingDuration = Effect.RemainingDuration - DeltaTime;
				if (Effect.RemainingDuration <= FFixedPoint::Zero)
				{
					Expired.Add(Effect.EffectInstanceID);
					continue;
				}
			}

			// Periodic tick — fire OnTick on roll-over (supports multiple fires per tick
			// in case DeltaTime > TickInterval, though that's unusual at 30 Hz).
			if (Def->TickInterval > FFixedPoint::Zero)
			{
				Effect.TimeSinceLastPeriodic = Effect.TimeSinceLastPeriodic + DeltaTime;
				while (Effect.TimeSinceLastPeriodic >= Def->TickInterval)
				{
					Effect.TimeSinceLastPeriodic = Effect.TimeSinceLastPeriodic - Def->TickInterval;
					USeinEffect* MutableCDO = Cast<USeinEffect>(Effect.EffectClass->GetDefaultObject());
					if (MutableCDO)
					{
						MutableCDO->OnTick(Effect.Target, Def->TickInterval);
					}
				}
			}
		}

		for (uint32 ExpiredID : Expired)
		{
			World.RemoveInstanceEffect(Handle, ExpiredID, /*bByExpiration=*/true);
		}
	}

	/** Tick a player-scope array (Class/Player). Hook dispatch uses the stored
	 *  per-effect Target (the entity that drove the apply). */
	static void TickPlayerScopedArray(TArray<FSeinActiveEffect>& Effects, FFixedPoint DeltaTime, USeinWorldSubsystem& World)
	{
		TArray<TPair<FSeinEntityHandle, uint32>> Expired;

		for (FSeinActiveEffect& Effect : Effects)
		{
			const USeinEffect* Def = Effect.EffectClass ? GetDefault<USeinEffect>(Effect.EffectClass) : nullptr;
			if (!Def) { continue; }

			if (Def->DurationMode == ESeinEffectDurationMode::Timed)
			{
				Effect.RemainingDuration = Effect.RemainingDuration - DeltaTime;
				if (Effect.RemainingDuration <= FFixedPoint::Zero)
				{
					Expired.Add({ Effect.Target, Effect.EffectInstanceID });
					continue;
				}
			}

			if (Def->TickInterval > FFixedPoint::Zero)
			{
				Effect.TimeSinceLastPeriodic = Effect.TimeSinceLastPeriodic + DeltaTime;
				while (Effect.TimeSinceLastPeriodic >= Def->TickInterval)
				{
					Effect.TimeSinceLastPeriodic = Effect.TimeSinceLastPeriodic - Def->TickInterval;
					USeinEffect* MutableCDO = Cast<USeinEffect>(Effect.EffectClass->GetDefaultObject());
					if (MutableCDO)
					{
						MutableCDO->OnTick(Effect.Target, Def->TickInterval);
					}
				}
			}
		}

		for (const TPair<FSeinEntityHandle, uint32>& E : Expired)
		{
			World.RemoveInstanceEffect(E.Key, E.Value, /*bByExpiration=*/true);
		}
	}
};
