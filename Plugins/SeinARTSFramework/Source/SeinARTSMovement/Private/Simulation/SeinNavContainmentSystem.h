/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavContainmentSystem.h
 * @brief   PostTick pass that keeps movable colliders ON the walkable area after
 *          collision resolution.
 *
 *          The collision floor (FSeinCollisionResolutionSystem) is deliberately
 *          nav-pure: it separates colliders along their minimum-translation axis
 *          with no concept of walkability, so it can shove a unit straight into a
 *          baked wall or off the edge of the nav grid — geometry that blocks NAV
 *          but carries no collider. This pass closes that gap from the MOVEMENT
 *          side, which is allowed to know nav (USeinMoveToAction's
 *          ResolveNavCollision already nav-clamps ordered steps the same way):
 *          any movable collider whose post-collision position is non-walkable is
 *          projected back onto the nearest walkable cell.
 *
 *          The collision/nav separation stays intact — collision never learns
 *          about nav; movement owns "units stay on nav."
 *
 *          NOT REDUNDANT with the collision floor's hard-barrier gate (the
 *          PassableResolver check in the resolver). That gate PREVENTS a collision
 *          push from crossing a static barrier; this pass is the broader net that
 *          CORRECTS a movable collider found off-nav from ANY cause — spawned/placed
 *          off the bake, a bake change under a standing unit, or any residual the
 *          per-push gate didn't cover. Prevent + correct, by design; keep both.
 *
 *          COVER EXEMPTION: a unit delivered to an AUTHORITATIVE destination (a
 *          cover slot that overrules the coarse bake) may legitimately stand on a
 *          bake-blocked cell, so positions the AuthoritativeDestinationResolver
 *          accepts are left untouched.
 *
 *          DETERMINISM: entity-pool order; pure per-unit (reads the deterministic
 *          nav bake + this unit's own transform, writes only this unit); all
 *          fixed-point — outcome is iteration-order-inert.
 *
 * Phase: PostTick | Priority: 11 — immediately after CollisionResolution (10),
 *        so it corrects the same tick's pushes before squad centroid (30) and
 *        the stable tick boundary.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Core/SeinParallel.h"
#include "Components/SeinExtentsComponent.h"
#include "SeinNavigation.h"
#include "SeinNavigationSubsystem.h"
#include "Types/Entity.h"
#include "Types/Vector.h"

class FSeinNavContainmentSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		// Nav required; absent / un-baked early in a level's life → no-op (the
		// next tick retries once the bake loads). Resolved through the subsystem
		// so this stays impl-agnostic — any USeinNavigation, not just the A*.
		USeinNavigation* Nav = USeinNavigationSubsystem::GetNavigationForWorld(&World);
		if (!Nav || !Nav->HasRuntimeData()) return;

		const ISeinComponentStorage* ExtentsStorage =
			World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
		if (!ExtentsStorage) return;

		const bool bHasAuthoritative = World.AuthoritativeDestinationResolver.IsBound();

		// Gather live handles, then project off-nav movable colliders back on. Pure
		// per-unit: reads the immutable nav bake + this tick's FROZEN dynamic-blocker
		// list (IsWorldPositionClear / ProjectPointToNav are scratch-free const reads;
		// the blocker list is stamped at PreTick 7 and never mutated during this
		// PostTick pass, so parallel reads are race-free) + own transform, writes only
		// own transform — a clean SeinParallelFor body. EXCEPTION: when a cover AuthoritativeDestination-
		// Resolver is bound, that cross-module delegate's thread-safety isn't
		// guaranteed, so cover projects run this serial via bForceSerial (then the
		// Execute call only ever runs on the main thread). `Sein.Sim.Parallel 0`
		// forces serial too; the result is bit-identical either way.
		TArray<FSeinEntityHandle> LiveHandles;
		LiveHandles.Reserve(World.GetEntityPool().GetActiveCount());
		World.GetEntityPool().ForEachEntity(
			[&LiveHandles](
				FSeinEntityHandle Handle,
				const FSeinEntity&)
			{
				LiveHandles.Add(Handle);
			});

		FSeinEntityPool* MutablePool =
			World.GetEntityPoolMutable();
		if (!MutablePool) return;

		SeinParallelFor(LiveHandles.Num(), [&](int32 Index)
		{
			const FSeinEntityHandle Handle = LiveHandles[Index];
			FSeinEntity* EntityPtr = MutablePool->Get(Handle);
			if (!EntityPtr) return;
			FSeinEntity& Entity = *EntityPtr;

			const FSeinExtentsComponent* Ext =
				static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(Handle));
			// Only MOVABLE colliders can be displaced off-nav by the floor.
			if (!Ext || !Ext->bCollisionEnabled || Ext->Shapes.Num() == 0) return;
			if (Ext->Mobility != ESeinCollisionMobility::Movable) return;

			const FFixedVector Pos = Entity.Transform.GetLocation();

			// Already on clear ground → nothing to do (the overwhelming common case).
			// DYNAMIC-aware: IsWorldPositionClear rejects the static bake AND the runtime
			// dynamic-blocker list (bBlocksNav), so this corrective net now extracts a unit
			// LEFT STANDING on a non-baked cover wall / deployable dropped over it — the
			// idle-side twin of the collision-floor and movement-floor fixes. Static
			// IsPassable saw only the bake and left such a unit stuck inside the wall.
			// Ground layer 0x01 matches the DynamicPassableResolver binding. (The
			// ProjectPointToNav pull-back below is still static, so it lands on a
			// bake-walkable cell that a dynamic blocker could also cover — that self-
			// corrects on the next tick's re-detection; a dynamic-aware projection is a
			// later refinement.)
			if (Nav->IsWorldPositionClear(Pos, /*AgentNavLayerMask=*/0x01)) return;

			// Cover-slot exemption: a unit on an authoritative destination may
			// stand on a bake-blocked cell — leave it where it is.
			if (bHasAuthoritative && World.AuthoritativeDestinationResolver.Execute(Pos)) return;

			// Shoved off the walkable area (into a baked wall, or off the nav
			// edge) → pull back onto the nearest walkable cell. ProjectPointToNav
			// fails only when there's no reachable cell at all (no bake / fully
			// sealed pocket) — there we leave the unit put rather than teleport it.
			FFixedVector Projected;
			if (Nav->ProjectPointToNav(Pos, Projected))
			{
				// Planar correction only — vertical (ground snap) is the movement
				// tick's job; preserve the unit's current Z.
				Projected.Z = Pos.Z;
				Entity.Transform.SetLocation(Projected);
			}
		}, /*bForceSerial=*/bHasAuthoritative);
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.movement.nav_containment")),
			1u,
			ESeinTickPhase::PostTick,
			SeinSystemPriority::NavContainment);
	}
};
