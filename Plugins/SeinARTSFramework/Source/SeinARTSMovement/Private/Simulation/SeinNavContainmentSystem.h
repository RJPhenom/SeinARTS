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
 *        the state hash (100).
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

		ISeinComponentStorage* ExtentsStorage =
			World.GetComponentStorageRaw(FSeinExtentsComponent::StaticStruct());
		if (!ExtentsStorage) return;

		const bool bHasAuthoritative = World.AuthoritativeDestinationResolver.IsBound();

		// Gather live handles, then project off-nav movable colliders back on. Pure
		// per-unit: reads the immutable nav bake (IsPassable / ProjectPointToNav are
		// scratch-free const reads) + own transform, writes only own transform — a
		// clean SeinParallelFor body. EXCEPTION: when a cover AuthoritativeDestination-
		// Resolver is bound, that cross-module delegate's thread-safety isn't
		// guaranteed, so cover projects run this serial via bForceSerial (then the
		// Execute call only ever runs on the main thread). `Sein.Sim.Parallel 0`
		// forces serial too; the result is bit-identical either way.
		TArray<FSeinEntityHandle> LiveHandles;
		LiveHandles.Reserve(World.GetEntityPool().GetActiveCount());
		World.GetEntityPool().ForEachEntity([&LiveHandles](FSeinEntityHandle Handle, FSeinEntity&) { LiveHandles.Add(Handle); });

		SeinParallelFor(LiveHandles.Num(), [&](int32 Index)
		{
			const FSeinEntityHandle Handle = LiveHandles[Index];
			FSeinEntity* EntityPtr = World.GetEntityPool().Get(Handle);
			if (!EntityPtr) return;
			FSeinEntity& Entity = *EntityPtr;

			const FSeinExtentsComponent* Ext =
				static_cast<const FSeinExtentsComponent*>(ExtentsStorage->GetComponentRaw(Handle));
			// Only MOVABLE colliders can be displaced off-nav by the floor.
			if (!Ext || !Ext->bCollisionEnabled || Ext->Shapes.Num() == 0) return;
			if (Ext->Mobility != ESeinCollisionMobility::Movable) return;

			const FFixedVector Pos = Entity.Transform.GetLocation();

			// Already on walkable ground → nothing to do. The overwhelming common
			// case: one grid lookup, no write.
			if (Nav->IsPassable(Pos)) return;

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

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PostTick; }
	virtual int32 GetPriority() const override { return SeinSystemPriority::NavContainment; }
	virtual FName GetSystemName() const override { return TEXT("NavContainment"); }
};
