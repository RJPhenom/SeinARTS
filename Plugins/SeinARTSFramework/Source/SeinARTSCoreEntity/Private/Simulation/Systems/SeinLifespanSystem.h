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
#include "Simulation/SeinWorldSubsystem.h"
#include "Components/SeinLifespanData.h"

class FSeinLifespanSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		const int32 CurrentTick = World.GetCurrentTick();
		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& /*Entity*/)
		{
			const FSeinLifespanData* Lifespan = World.GetComponent<FSeinLifespanData>(Handle);
			if (Lifespan && CurrentTick >= Lifespan->ExpiresAtTick)
			{
				World.DestroyEntity(Handle);
			}
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	// Runs AFTER ProcessDeferredDestroys (which is the PostTick pre-step, not a
	// registered system) but before the state hash system — marks entities for
	// the next tick's destroy pass.
	virtual int32 GetPriority() const override { return -10; }
	virtual FName GetSystemName() const override { return TEXT("Lifespan"); }
};
