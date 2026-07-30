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
		// GetComponent miss. ForEachLiveComponent yields exact handles in the
		// same ascending-slot order ForEachEntity did, so destroy order is
		// unchanged and stale component occupants remain distinguishable.
		// DestroyEntity only DEFERS (adds to PendingDestroy + flags the entity
		// dead); it does not remove this storage's slot mid-iteration, so
		// walking the live bit-array stays safe.
		const ISeinComponentStorage* Storage =
			World.GetComponentStorageRaw(FSeinLifespanData::StaticStruct());
		if (!Storage) return;

		const int32 CurrentTick = World.GetCurrentTick();
		const FSeinEntityPool& Pool = World.GetEntityPool();
		Storage->ForEachLiveComponent([&](
			FSeinEntityHandle Handle,
			const void* RawComponent)
		{
			if (!Pool.IsValid(Handle))
			{
				return;
			}
			const FSeinLifespanData* Lifespan = static_cast<const FSeinLifespanData*>(RawComponent);
			if (Lifespan && CurrentTick >= Lifespan->ExpiresAtTick)
			{
				World.DestroyEntity(Handle);
			}
		});
	}

	// Runs after ProcessDeferredDestroys (the PostTick pre-step, not a
	// registered system) and marks entities for the next tick's destroy pass.
	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.lifespan")),
			1u,
			ESeinTickPhase::PostTick,
			SeinSystemPriority::Lifespan);
	}
};
