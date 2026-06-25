/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinStateHashSystem.h
 * @brief   Computes and logs the simulation state hash for desync detection.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#endif

/**
 * System: State Hash
 * Phase: PostTick | Priority: 100
 *
 * Computes World.ComputeStateHash() at the end of a simulation tick — but ONLY
 * when something actually consumes it, because a full reflection-walk of every
 * component storage every tick is pure waste otherwise.
 *
 * Who consumes the per-tick hash:
 *   - Networked lockstep desync gossip. The net subsystem computes its OWN
 *     hash on demand at each turn boundary (USeinNetSubsystem::MaybeSubmitStateHashCheck
 *     calls World.ComputeStateHash() directly) — it does NOT read this system's
 *     LastHash. But that path only runs in a live lockstep session, which is
 *     exactly when World.TurnReadyResolver is bound (the net subsystem binds it
 *     in NotifyLocalSlotAssigned, unbinds in Deinitialize; unbound = Standalone).
 *   - The Sein.Sim.StateHash.Log determinism-verification cvar (non-shipping),
 *     whose own per-tick dump in USeinWorldSubsystem::TickSimulation is separate
 *     from this system; this system's Verbose line is a redundant secondary log.
 *
 * Nothing reads LastHash / GetLastHash() in standalone single-player, so we
 * skip the computation entirely there. The gate ERRS TOWARD COMPUTING: if a
 * lockstep session is active OR the log cvar is on, we still compute, and the
 * result is byte-identical to the unconditional version.
 */
class FSeinStateHashSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Authoritative "networked lockstep session is live" signal: the net
		// subsystem binds TurnReadyResolver on slot assignment and unbinds it on
		// teardown. Standalone single-player never binds it.
		bool bShouldCompute = World.TurnReadyResolver.IsBound();

#if !UE_BUILD_SHIPPING
		// Determinism-verification logging — keep computing so the Verbose
		// stream stays available when a developer flips the cvar on.
		if (!bShouldCompute)
		{
			static IConsoleVariable* CVarLog =
				IConsoleManager::Get().FindConsoleVariable(TEXT("Sein.Sim.StateHash.Log"));
			bShouldCompute = CVarLog && CVarLog->GetInt() != 0;
		}
#endif

		if (!bShouldCompute)
		{
			return;
		}

		const int32 Hash = World.ComputeStateHash();
		LastHash = Hash;

		UE_LOG(LogTemp, Verbose,
			TEXT("[StateHash] Tick %d | Hash: 0x%08X"),
			World.GetCurrentTick(),
			static_cast<uint32>(Hash));
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return SeinSystemPriority::StateHash; }
	virtual FName GetSystemName() const override { return TEXT("StateHash"); }

	/** Last computed hash, accessible for networking comparison. */
	int32 GetLastHash() const { return LastHash; }

private:
	int32 LastHash = 0;
};
