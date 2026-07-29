/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolverDefault.h
 * @brief   The shipped default collision resolver: a fixed-pass Gauss-Seidel
 *          relaxation that separates overlapping COLLIDERS along their
 *          minimum-translation axis, so two Blocking colliders never end a tick
 *          inside each other — and a unit can never be shoved THROUGH a solid
 *          collider (a wall especially).
 *
 *          Separation is pure extent-vs-extent: the OVERLAP test consults ONLY
 *          the collision model (FSeinExtentsComponent's collision section + the
 *          channel registry) and the deterministic MTV narrowphase. The one nav
 *          touch-point is the HARD-BARRIER gate: a separation move that would land
 *          a unit on a non-walkable cell (a baked nav wall, or off the grid edge)
 *          is REFUSED — the unit holds at the barrier instead of being shoved
 *          across it. That walkability test goes through the world subsystem's
 *          pluggable PassableResolver delegate (cover slots exempt via
 *          AuthoritativeDestinationResolver), so the floor stays nav-impl-agnostic:
 *          it asks "walkable?" through a seam, it doesn't know any nav. Net result:
 *          sein-extents colliders block by the MTV separation; baked nav walls and
 *          the grid edge block by this gate; both are never-crossable.
 *
 *          Per tick: build the channel default-response table once, then run a
 *          fixed number of relaxation passes. Each pass iterates only MOVABLE
 *          colliders as "self" (statics never move, so they're only ever the
 *          queried neighbour), and for each Blocking pair pushes apart along the
 *          deepest shape-pair contact. Static neighbours are infinite-mass (the
 *          movable takes the entire push), which is what makes a wall un-passable.
 *
 *          GAUSS-SEIDEL: a pass reads each self's CURRENT (mid-pass) transform,
 *          so a self pushed earlier this pass is seen moved by its later pairs —
 *          fast convergence, but the in-pass reads are sequential, so the pass
 *          itself is serial. (The parallel-friendly alternative is the Parallel
 *          resolver — Jacobi relaxation — which reads a frozen snapshot and defers writes.)
 *
 *          The shared floor — narrowphase shape build, channel/collider/mass
 *          helpers, the hard-barrier CanOccupy gate, and the overlap-event diff —
 *          now lives on the USeinCollisionResolver base (used verbatim by every
 *          resolver). This class supplies ONLY the Gauss-Seidel separation pass
 *          (ResolvePass) and the per-tick driver (Resolve). The math, pass count,
 *          mass-weighting, hard-barrier gate, overlap-event diff, and narrowphase
 *          are unchanged from when they lived inline in
 *          FSeinCollisionResolutionSystem — only their host moved.
 */

#pragma once

#include "CoreMinimal.h"
#include "Collision/SeinCollisionResolver.h"
#include "Types/FixedPoint.h"
#include "SeinCollisionResolverDefault.generated.h"

class USeinWorldSubsystem;

/**
 * Resolves collision for sim-side entities synchronously (on the game thread). Keeps solid
 * bodies from ending a tick overlapping, and is the resolver selected out of the box.
 *
 * Uses GAUSS-SEIDEL relaxation: a fixed number of passes, each walking the movable colliders
 * one at a time and pushing every overlapping Blocking pair apart along their shortest-
 * separation (minimum-translation) axis. "Gauss-Seidel" means each body reads the latest
 * mid-pass positions as the pass proceeds — a body already nudged this pass is seen in its
 * moved spot by the bodies it meets later, so a dense pile settles in few passes; the trade-off
 * is that those sequential reads keep the pass single-threaded. Candidate neighbours come from
 * the collision broadphase (rebuilt each PreTick), so the cost stays roughly linear in the
 * collider count. Pushes are mass-weighted (a heavier body barely yields to a lighter one;
 * walls and statics are infinite-mass), and a hard-barrier gate refuses any push that would
 * cross a baked wall or leave the grid edge. Runs after movement each tick, so its settled
 * separations are part of the deterministic snapshot. For the multithreaded
 * variant see Sein Collision Resolver (Parallel).
 */
UCLASS(meta = (DisplayName = "Sein Collision Resolver (Default)"))
class SEINARTSCOREENTITY_API USeinCollisionResolverDefault : public USeinCollisionResolver
{
	GENERATED_BODY()

public:
	/** One tick's full resolution: the fixed relaxation passes followed by the
	 *  overlap-event diff. Body relocated verbatim from the old
	 *  FSeinCollisionResolutionSystem::Tick. */
	virtual void Resolve(USeinWorldSubsystem& World) override;

private:
	/** One relaxation pass: separate every Block pair it touches. Reads each
	 *  self's mid-pass transform (Gauss-Seidel) and writes pushes immediately,
	 *  so it must run serially. */
	static void ResolvePass(USeinWorldSubsystem& World, const TMap<FName, ESeinCollisionResponse>& ChannelDefaults, const FFixedPoint MassRatioCutoff);
};
