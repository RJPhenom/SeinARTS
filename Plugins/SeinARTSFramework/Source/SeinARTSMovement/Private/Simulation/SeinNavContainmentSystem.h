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
 *          AUTHORITY EXEMPTION: a unit delivered to an authoritative destination
 *          may legitimately stand on a bake-blocked cell, so positions accepted
 *          by the composed provider registry are left untouched.
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
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinNavigationPayload.h"
#include "Movement/SeinMovement.h"
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
			World.GetComponentStorageRaw(FSeinExtentsPayload::StaticStruct());
		if (!ExtentsStorage) return;
		const ISeinComponentStorage* NavigationStorage =
			World.GetComponentStorageRaw(
				FSeinNavigationPayload::StaticStruct());

		const bool bHasAuthoritative =
			World.HasAuthoritativeDestinationProviders();

		// Gather live handles, then project off-nav movable colliders back on. Pure
		// per-unit: reads the immutable nav bake + this tick's FROZEN dynamic-blocker
		// list (IsWorldPositionClear / ProjectPointToNav are scratch-free const reads;
		// the blocker list is stamped at PreTick 7 and never mutated during this
		// PostTick pass, so parallel reads are race-free) + own transform, writes only
		// own transform — a clean SeinParallelFor body. EXCEPTION: registered
		// authoritative-destination callbacks are game-thread-only, so worlds with
		// providers run this pass serially. `Sein.Sim.Parallel 0`
		// forces serial too; the result is bit-identical either way.
		TArray<FSeinEntityHandle> LiveHandles;
		LiveHandles.Reserve(ExtentsStorage->GetComponentCount());
		ExtentsStorage->ForEachLiveComponent(
			[&LiveHandles, &World](
				FSeinEntityHandle Handle,
				const void* RawComponent)
			{
				const FSeinExtentsPayload* Ext =
					static_cast<const FSeinExtentsPayload*>(RawComponent);
				if (World.GetEntityPool().IsValid(Handle) && Ext
					&& Ext->bCollisionEnabled && !Ext->Shapes.IsEmpty()
					&& Ext->Mobility == ESeinCollisionMobility::Movable)
				{
					LiveHandles.Add(Handle);
				}
			});

		const FSeinEntityPool& EntityPool = World.GetEntityPool();
		TArray<FFixedVector> CorrectedPositions;
		CorrectedPositions.SetNum(LiveHandles.Num());
		TArray<uint8> NeedsCorrection;
		NeedsCorrection.SetNumZeroed(LiveHandles.Num());

		SeinParallelFor(LiveHandles.Num(), [&](int32 Index)
		{
			const FSeinEntityHandle Handle = LiveHandles[Index];
			const FSeinEntity* EntityPtr = EntityPool.Get(Handle);
			if (!EntityPtr) return;
			const FSeinEntity& Entity = *EntityPtr;

			const FSeinExtentsPayload* Ext =
				static_cast<const FSeinExtentsPayload*>(ExtentsStorage->GetComponentRaw(Handle));
			// Only MOVABLE colliders can be displaced off-nav by the floor.
			if (!Ext || !Ext->bCollisionEnabled || Ext->Shapes.Num() == 0) return;
			if (Ext->Mobility != ESeinCollisionMobility::Movable) return;

			const FFixedVector Pos = Entity.Transform.GetLocation();
			const FSeinNavigationPayload* NavData =
				NavigationStorage
				? static_cast<const FSeinNavigationPayload*>(
					NavigationStorage->GetComponentRaw(Handle))
				: nullptr;
			FSeinNavAgentProfile Agent;
			Agent.Requester = Handle;
			Agent.AgentFootprintRadius =
				USeinMovement::ResolveCollisionRadius(
					Ext, NavData);
			if (NavData)
			{
				Agent.BlockedTerrainTags =
					NavData->BlockedTerrainTags;
				Agent.AgentNavLayerMask =
					NavData->NavLayerMask;
				Agent.AgentWallPaddingCells =
					NavData->WallPadding;
			}

			// Already on clear ground → nothing to do (the overwhelming common case).
			// DYNAMIC-aware: IsWorldPositionClear rejects the static bake AND the runtime
			// dynamic-blocker list (bBlocksNav), so this corrective net now extracts a unit
			// LEFT STANDING on a non-baked cover wall / deployable dropped over it — the
			// idle-side twin of the collision-floor and movement-floor fixes. Static
			// IsPassable saw only the bake and left such a unit stuck inside the wall.
			if (Nav->IsFootprintClearForAgent(Pos, Agent)) return;

			// An exact authoritative destination may stand on a bake-blocked cell.
			if (bHasAuthoritative
				&& World.IsAuthoritativeDestination(Pos, Handle)
				&& Nav->IsAuthoritativeFootprintSafeForAgent(
					Pos, Agent))
			{
				return;
			}

			// Shoved off the walkable area (into a baked wall, or off the nav
			// edge) → pull back onto the nearest walkable cell. ProjectPointToNav
			// fails only when there's no reachable cell at all (no bake / fully
			// sealed pocket) — there we leave the unit put rather than teleport it.
			FFixedVector Projected;
			if (Nav->ProjectPointToNavForAgent(
				Pos, Agent, Projected))
			{
				// Planar correction only — vertical (ground snap) is the movement
				// tick's job; preserve the unit's current Z.
				Projected.Z = Pos.Z;
				if (Projected != Pos)
				{
					CorrectedPositions[Index] = Projected;
					NeedsCorrection[Index] = 1;
				}
			}
		}, /*bForceSerial=*/bHasAuthoritative);

		// Canonical serial apply: only entities whose location actually changed
		// receive mutable access (and therefore a mutation revision). This also
		// avoids racing the revision counter from parallel worker bodies.
		for (int32 Index = 0; Index < LiveHandles.Num(); ++Index)
		{
			if (NeedsCorrection[Index] == 0)
			{
				continue;
			}
			if (FSeinEntity* Entity =
				World.GetEntityMutable(LiveHandles[Index]))
			{
				Entity->Transform.SetLocation(CorrectedPositions[Index]);
			}
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.movement.nav_containment")),
			2u,
			ESeinTickPhase::PostTick,
			SeinSystemPriority::NavContainment);
	}
};
