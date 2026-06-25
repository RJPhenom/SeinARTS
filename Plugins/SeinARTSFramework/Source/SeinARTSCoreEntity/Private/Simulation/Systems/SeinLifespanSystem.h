/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLifespanSystem.h
 * @brief   Reaps entities whose FSeinLifespanData::ExpiresAtTick has passed.
 *          Runs PostTick; the actual destroy happens via USeinWorldSubsystem's
 *          deferred destroy list, reaped on the NEXT tick's PostTick.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/ComponentStorage.h"
#include "Components/SeinLifespanData.h"

class FSeinLifespanSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Only entities carrying FSeinLifespanData can expire — iterate that
		// storage's live slots directly instead of the whole pool + per-entity
		// GetComponent miss. ForEachLiveComponent yields slots in the same
		// ascending order ForEachEntity did, so the destroy order is unchanged.
		// DestroyEntity only DEFERS (adds to PendingDestroy + flags the entity
		// dead); it does not remove this storage's slot mid-iteration, so
		// walking the live bit-array stays safe.
		ISeinComponentStorage* Storage =
			World.GetComponentStorageRaw(FSeinLifespanData::StaticStruct());
		if (!Storage) return;

		const int32 CurrentTick = World.GetCurrentTick();
		FSeinEntityPool& Pool = World.GetEntityPool();
		Storage->ForEachLiveComponent([&](int32 SlotIndex, void* RawComponent)
		{
			const FSeinLifespanData* Lifespan = static_cast<const FSeinLifespanData*>(RawComponent);
			if (Lifespan && CurrentTick >= Lifespan->ExpiresAtTick)
			{
				const FSeinEntityHandle Handle(SlotIndex, Pool.GetSlotGeneration(SlotIndex));
				World.DestroyEntity(Handle);
			}
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	// Runs AFTER ProcessDeferredDestroys (which is the PostTick pre-step, not a
	// registered system) but before the state hash system — marks entities for
	// the next tick's destroy pass.
	virtual int32 GetPriority() const override { return SeinSystemPriority::Lifespan; }
	virtual FName GetSystemName() const override { return TEXT("Lifespan"); }
};
