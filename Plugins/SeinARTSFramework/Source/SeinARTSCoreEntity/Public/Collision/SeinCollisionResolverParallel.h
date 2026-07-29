/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolverParallel.h
 * @brief   Parallel collision resolver: a fixed-pass JACOBI relaxation that
 *          separates overlapping Block colliders — the same extent-vs-extent
 *          floor as the shipped Gauss-Seidel default, but structured so each
 *          pass's per-mover compute is a deterministic SeinParallelFor.
 *
 *          GAUSS-SEIDEL vs JACOBI. The default reads each self's CURRENT
 *          (mid-pass) transform, so a self pushed earlier this pass is already
 *          moved when its later pairs run — fast convergence, but the in-pass
 *          reads are sequential, so the pass cannot be parallelized. The Jacobi
 *          pass instead reads a FROZEN start-of-pass snapshot for BOTH self and
 *          every neighbour, accumulates each mover's own separation into a
 *          scratch slot, and applies all moves AFTER the whole pass. No mover
 *          ever reads another mover's mid-pass move, so the per-mover compute is
 *          a pure function of immutable state writing a disjoint slot — exactly
 *          the SeinParallelFor body contract. The trade-off is convergence
 *          speed: because neighbours don't see each other's pushes within a pass,
 *          Jacobi needs MORE passes to settle a dense cluster (default 8 vs the
 *          GS 4).
 *
 *          PER-SIDE SHARE (the other structural difference). Gauss-Seidel resolves
 *          a pair ONCE (from the lower index) and pushes BOTH sides. Jacobi has no
 *          pair-once gate: every mover independently computes ONLY ITS OWN share of
 *          every overlap it is in, so the two sides of a pair are resolved in two
 *          separate mover computes. Summed over the pass the net separation matches
 *          — and dropping the cross-mover write is precisely what lets the compute
 *          parallelize.
 *
 *          SHARED FLOOR. The narrowphase shape build, channel/collider/mass
 *          helpers, the hard-barrier CanOccupy gate (per-push: a push that would
 *          cross a wall / the grid edge is refused, body holds), and the
 *          overlap-event diff are the SAME protected base members the default uses
 *          — bit-for-bit the same collision model, only the relaxation schedule
 *          differs.
 *
 *          DEFAULT-OFF. The framework default stays the Gauss-Seidel resolver;
 *          this is selected by pointing `USeinARTSCoreSettings::CollisionResolverClass`
 *          at it. Determinism is provable on the `Sein.Sim.Parallel 0-vs-1`
 *          canonical-root gate: the parallel compute is bit-identical to the serial
 *          fallback (immutable reads, disjoint writes, fixed-point sums,
 *          handle-sorted neighbour loop, serial apply).
 */

#pragma once

#include "CoreMinimal.h"
#include "Collision/SeinCollisionResolver.h"
#include "Types/FixedPoint.h"
#include "SeinCollisionResolverParallel.generated.h"

class USeinWorldSubsystem;

/**
 * Resolves collision for sim-side entities across worker threads. Same job as the default
 * resolver — stop solid bodies overlapping — structured so the heavy per-body compute fans out
 * in parallel for large unit counts.
 *
 * Uses JACOBI relaxation. Where Gauss-Seidel reads each body's latest mid-pass position (quick
 * to settle, but sequential), Jacobi freezes a start-of-pass snapshot, has every body compute
 * only its OWN separation from that frozen state into a private slot, then applies all the
 * moves together once the pass ends. No body ever reads another's mid-pass move, so each body's
 * work is a pure function of immutable state writing a disjoint slot — the contract that lets
 * the pass run as a deterministic parallel-for. The trade-off is convergence: because
 * neighbours don't see each other's pushes within a pass, Jacobi needs more passes to settle a
 * dense cluster (default 8, versus 4 for Gauss-Seidel). Everything else — the mass-weighting,
 * the infinite-mass walls and statics, the hard-barrier gate that refuses a push through a
 * baked wall or off the grid, the overlap events — is the exact same collision floor as the
 * default; only the relaxation schedule differs, and the result is bit-identical to the serial
 * path (provable on the Sein.Sim.Parallel 0-vs-1 canonical-root gate). Runs after movement
 * during PostTick, so settled positions are visible at the stable tick boundary.
 */
UCLASS(meta = (DisplayName = "Sein Collision Resolver (Parallel)"))
class SEINARTSCOREENTITY_API USeinCollisionResolverParallel : public USeinCollisionResolver
{
	GENERATED_BODY()

public:
	/** One tick's full resolution: NumPasses Jacobi relaxation passes followed by
	 *  the shared overlap-event diff. */
	virtual void Resolve(USeinWorldSubsystem& World) override;

	/** Relaxation passes per tick. Jacobi converges slower than Gauss-Seidel
	 *  (neighbours don't see each other's in-pass pushes), so this defaults
	 *  HIGHER than the GS resolver's fixed 4. */
	UPROPERTY(EditDefaultsOnly, Category = "Collision", meta = (ClampMin = "1"))
	int32 NumPasses = 8;

	/** Scales every accepted push (1 = no relaxation). <1 under-relaxes for extra
	 *  stability in very dense piles at the cost of more passes to settle; >1 is
	 *  not recommended (can overshoot). Fixed-point so it stays deterministic. */
	UPROPERTY(EditDefaultsOnly, Category = "Collision")
	FFixedPoint Relaxation = FFixedPoint::One;

private:
	/** One Jacobi relaxation pass: gather movers (serial), compute each mover's
	 *  own barrier-gated separation from the FROZEN snapshot (parallel), then
	 *  apply all moves (serial, disjoint). */
	void JacobiPass(USeinWorldSubsystem& World, const TMap<FName, ESeinCollisionResponse>& ChannelDefaults, const FFixedPoint MassRatioCutoff);
};
