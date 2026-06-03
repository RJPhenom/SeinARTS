/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStateHashSystem.h
 * @brief   Computes and logs the simulation state hash for desync detection.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"

/**
 * System: State Hash
 * Phase: PostTick | Priority: 100
 *
 * Calls World.ComputeStateHash() at the end of every simulation tick
 * and logs the result. In networked games the hash can be compared
 * across clients to detect lockstep desynchronisation.
 */
class FSeinStateHashSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		const int32 Hash = World.ComputeStateHash();
		LastHash = Hash;

		UE_LOG(LogTemp, Verbose,
			TEXT("[StateHash] Tick %d | Hash: 0x%08X"),
			World.GetCurrentTick(),
			static_cast<uint32>(Hash));
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return 100; }
	virtual FName GetSystemName() const override { return TEXT("StateHash"); }

	/** Last computed hash, accessible for networking comparison. */
	int32 GetLastHash() const { return LastHash; }

private:
	int32 LastHash = 0;
};
