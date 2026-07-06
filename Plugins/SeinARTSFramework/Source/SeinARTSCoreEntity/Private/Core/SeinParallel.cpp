/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinParallel.cpp
 * @brief   Backing state for the SeinParallelFor primitive: the two governing
 *          cvars and the global parallel-section flag.
 */

#include "Core/SeinParallel.h"
#include "HAL/IConsoleManager.h"

#include <atomic>

namespace
{
	int32 GSeinSimParallel = 1;
	FAutoConsoleVariableRef CVarSeinSimParallel(
		TEXT("Sein.Sim.Parallel"),
		GSeinSimParallel,
		TEXT("Master toggle for deterministic sim parallelism (SeinParallelFor).\n")
		TEXT("  1 (default) = parallel sim passes (avoidance, idle-driver, nav-containment, pathfinding...) run multithreaded.\n")
		TEXT("  0           = force every SeinParallelFor serial. The parallel path is designed to be BIT-IDENTICAL to this;\n")
		TEXT("                diff Sein.Sim.StateHash.Log streams with this 1 vs 0 over the same scenario to verify."),
		ECVF_Default);

	int32 GSeinSimParallelMinBatch = 64;
	FAutoConsoleVariableRef CVarSeinSimParallelMinBatch(
		TEXT("Sein.Sim.ParallelMinBatch"),
		GSeinSimParallelMinBatch,
		TEXT("Minimum element count for a SeinParallelFor to actually go parallel. Smaller batches run serially\n")
		TEXT("(thread-dispatch overhead exceeds the win below a few dozen entities). Default 64."),
		ECVF_Default);

	int32 GSeinSimAsyncPathfinding = 1;
	FAutoConsoleVariableRef CVarSeinSimAsyncPathfinding(
		TEXT("Sein.Sim.AsyncPathfinding"),
		GSeinSimAsyncPathfinding,
		TEXT("Run path requests as a deterministic BATCH one tick after they're made, instead of\n")
		TEXT("inline-synchronous. NOT gated on Sein.Sim.Parallel: the batch runs parallel when Parallel\n")
		TEXT("is on and byte-identically serial when off, so the deferred timing is the same on every\n")
		TEXT("peer regardless of that per-machine toggle. Cached results are keyed by request CONTENT\n")
		TEXT("(destination + agent params, excluding the per-tick-resampled Start), so a re-ordered unit\n")
		TEXT("never follows a stale path. DEFAULT ON. SIM-AFFECTING: changes WHICH tick a unit gets its\n")
		TEXT("path, so it must match across all clients (the SeinARTS project setting drives it as a\n")
		TEXT("fingerprinted build default)."),
		ECVF_Default);
}

bool SeinSimParallelEnabled()
{
	return GSeinSimParallel != 0;
}

int32 SeinSimParallelMinBatch()
{
	return GSeinSimParallelMinBatch;
}

bool SeinSimAsyncPathfindingEnabled()
{
	return GSeinSimAsyncPathfinding != 0;
}

#if !UE_BUILD_SHIPPING

namespace
{
	// GLOBAL (not thread-local): the primitive raises this on the calling thread
	// before dispatch and lowers it after the join, so every worker thread
	// running a body observes it. A depth counter tolerates the (discouraged)
	// nested-dispatch case gracefully.
	std::atomic<int32> GSeinParallelDepth{ 0 };
}

bool SeinIsInParallelSection()
{
	return GSeinParallelDepth.load(std::memory_order_relaxed) > 0;
}

void SeinSetInParallelSection(bool bInSection)
{
	if (bInSection)
	{
		GSeinParallelDepth.fetch_add(1, std::memory_order_relaxed);
	}
	else
	{
		GSeinParallelDepth.fetch_sub(1, std::memory_order_relaxed);
	}
}

#endif
