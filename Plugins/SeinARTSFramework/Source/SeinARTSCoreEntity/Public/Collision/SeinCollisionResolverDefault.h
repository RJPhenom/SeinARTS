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
 * Default collision resolver (Gauss-Seidel).
 *
 * Runs after movement (PostTick) and before StateHash, so its separations are
 * part of the deterministic state snapshot. Uses the collision broadphase
 * (rebuilt PreTick) to find candidate neighbours in O(K).
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
