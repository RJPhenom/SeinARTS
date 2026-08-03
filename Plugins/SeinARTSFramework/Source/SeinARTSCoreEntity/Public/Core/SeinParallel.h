/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinParallel.h
 * @brief   Deterministic parallel-for primitive + in-sim parallel-section guard.
 *
 *          The sim is a deterministic lockstep simulation: every client must
 *          compute byte-identical state each tick regardless of hardware OR
 *          worker-thread count (a 4-core and a 16-core client must agree).
 *          `SeinParallelFor` lets a sim system fan a per-entity loop across
 *          worker threads while preserving that guarantee — but ONLY when the
 *          loop body obeys the contract below. Parallelism here is a pure
 *          wall-clock optimization that is INVISIBLE to sim logic: the result
 *          is bit-identical to running the loop serially.
 *
 *          THE CONTRACT (every SeinParallelFor body MUST obey):
 *            1. READ ONLY IMMUTABLE STATE — the prior-phase-finalized world
 *               (start-of-tick transforms, the baked nav grid, a structure no
 *               other system mutates during this pass). Never read state a
 *               concurrent body is writing.
 *            2. WRITE ONLY YOUR OWN SLOT — Body(i) writes only entity i's own
 *               components / its own output slot. Never write another entity,
 *               never a shared structure.
 *            3. NO SHARED / STRUCTURAL MUTATION — no SpawnEntity / DestroyEntity
 *               / AddComponent (storage realloc) / EnqueueCommand /
 *               EnqueueVisualEvent / shared accumulator / shared PRNG inside the
 *               body. Cross-entity effects are deferred to the serial tick spine
 *               (the "parallel-compute -> serial-apply" pattern).
 *            4. NO FLOAT, NO WALL-CLOCK — fixed-point only (FFixedPoint sums are
 *               order-independent; float is not). No real-time/Date reads.
 *
 *          Why it's deterministic: Body(i) is then a PURE FUNCTION of immutable
 *          inputs writing a disjoint slot, so the result cannot depend on which
 *          thread ran which i, nor on how many threads exist.
 *
 *          ENFORCEMENT: in non-shipping builds the primitive raises a global
 *          parallel-section flag for the duration of the dispatch; the sim's
 *          structural mutators assert `SEIN_CHECK_NOT_PARALLEL()` so a rule-3
 *          violation trips immediately at the call site, on ANY thread.
 *
 *          GATING (so it can be A/B'd against the serial path in-editor):
 *            Sein.Sim.Parallel          (default 1) master on/off. 0 forces
 *                                        every SeinParallelFor serial. The
 *                                        parallel path is designed to be BIT-
 *                                        IDENTICAL to this — compare canonical
 *                                        world roots plus peer/replay agreement
 *                                        with it 1 vs 0 over the same scenario.
 *            Sein.Sim.ParallelMinBatch  (default 64) batches smaller than this
 *                                        run serial (dispatch overhead > win).
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/ParallelFor.h"

/** Master toggle (Sein.Sim.Parallel != 0). Read once per dispatch on the
 *  calling thread. */
SEINARTSCOREENTITY_API bool SeinSimParallelEnabled();

/** Minimum element count for a dispatch to actually go parallel
 *  (Sein.Sim.ParallelMinBatch). Smaller batches run serially. */
SEINARTSCOREENTITY_API int32 SeinSimParallelMinBatch();

/** Async pathfinding toggle (Sein.Sim.AsyncPathfinding != 0). When set, path requests are
 *  queued and run as deterministic budgeted batches beginning on the next tick instead of
 *  inline-synchronous. Reads
 *  ONLY the async cvar, NOT Sein.Sim.Parallel: the batch runs parallel when Parallel is on and
 *  byte-identically serial when off, so the deferred (sim-affecting, fingerprinted) timing is the
 *  same on every peer regardless of the per-machine Parallel toggle. Default ON. */
SEINARTSCOREENTITY_API bool SeinSimAsyncPathfindingEnabled();

#if !UE_BUILD_SHIPPING

/** True while a SeinParallelFor dispatch is in flight. Set by the primitive on
 *  the calling thread and cleared after the join — it is a GLOBAL (not thread-
 *  local) flag precisely so a worker-thread body that illegally calls a
 *  structural mutator is caught from whichever thread runs it. Mirrors the
 *  SeinSimContext flag pattern. */
SEINARTSCOREENTITY_API bool SeinIsInParallelSection();
SEINARTSCOREENTITY_API void SeinSetInParallelSection(bool bInSection);

/** Place on structural/shared sim mutators (spawn/destroy/AddComponent/enqueue)
 *  that MUST run on the serial tick spine. Trips if reached from inside a
 *  SeinParallelFor body (contract rule 3). */
#define SEIN_CHECK_NOT_PARALLEL() checkf(!SeinIsInParallelSection(), \
	TEXT("Illegal shared/structural sim mutation inside a SeinParallelFor body: %s — defer it to the serial tick spine (parallel-compute, serial-apply)."), \
	ANSI_TO_TCHAR(__FUNCTION__))

#else

#define SEIN_CHECK_NOT_PARALLEL()

#endif

/**
 * Deterministic parallel-for. Runs Body(i) for i in [0, Count). Falls back to a
 * plain serial loop when bForceSerial is set, the master cvar is off, or
 * Count < Sein.Sim.ParallelMinBatch. Read the file header for the body contract
 * — violating it breaks lockstep determinism (SEIN_CHECK_NOT_PARALLEL traps the
 * structural-mutation subset in non-shipping builds).
 *
 * @param Count        number of iterations (entities / output slots)
 * @param Body         callable invoked as Body(int32 Index)
 * @param bForceSerial per-call override to run serially regardless of the cvar
 */
template <typename FunctionType>
FORCEINLINE void SeinParallelFor(int32 Count, FunctionType&& Body, bool bForceSerial = false)
{
	if (Count <= 0)
	{
		return;
	}

	const bool bSerial = bForceSerial || (Count < SeinSimParallelMinBatch()) || !SeinSimParallelEnabled();
	if (bSerial)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Body(Index);
		}
		return;
	}

#if !UE_BUILD_SHIPPING
	SeinSetInParallelSection(true);
#endif

	ParallelFor(Count, [&Body](int32 Index) { Body(Index); });

#if !UE_BUILD_SHIPPING
	SeinSetInParallelSection(false);
#endif
}
