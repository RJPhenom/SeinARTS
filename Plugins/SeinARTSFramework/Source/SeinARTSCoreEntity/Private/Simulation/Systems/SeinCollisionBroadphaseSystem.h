/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionBroadphaseSystem.h
 * @brief   PreTick system that rebuilds the collision broadphase
 *          (FSeinCollisionSpatialHash) from current collider positions.
 *          Replaces the old generic FSeinSpatialHashSystem — which gated on a
 *          navigation component and existed only to feed penetration. This one
 *          is purely collision-driven and has NO navigation dependency.
 *
 *          Each tick: rebuild the dynamic tier from every enabled Movable
 *          collider; rebuild the static tier too, but only on ticks where the
 *          static set changed (Hash.IsStaticDirty()). A collider is any entity
 *          whose FSeinExtentsComponent has bCollisionEnabled, at least one
 *          Shape, and a non-None ObjectType.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Collision/SeinCollisionSpatialHash.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"  // GetColliderBoundingRadius — shared with the resolver

/**
 * System: Collision Broadphase Rebuild
 * Phase: PreTick | Priority: 5
 *
 * Runs before the collision resolver (PostTick) so neighbour queries see this
 * tick's positions. Full clear+rebuild of the dynamic tier each tick; the
 * static tier is only rebuilt when dirty, so maps with lots of static geometry
 * (walls/buildings) pay for them once, not every tick.
 */
class FSeinCollisionBroadphaseSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		FSeinCollisionSpatialHash& Hash = World.GetCollisionSpatialHash();

		const bool bRebuildStatic = Hash.IsStaticDirty();
		if (bRebuildStatic)
		{
			Hash.ClearStatic();
		}
		Hash.ClearDynamic();

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			const FSeinExtentsComponent* Extents = World.GetComponent<FSeinExtentsComponent>(Handle);
			if (!Extents || !Extents->bCollisionEnabled) return;
			if (Extents->Shapes.Num() == 0 || Extents->ObjectType.Channel.IsNone()) return;

			const FFixedVector Pos = Entity.Transform.GetLocation();
			const FFixedPoint Radius = SeinExtentsHelpers::GetColliderBoundingRadius(*Extents);
			if (Extents->Mobility == ESeinCollisionMobility::Static)
			{
				// Static positions don't change; only (re)insert on a dirty pass.
				if (bRebuildStatic)
				{
					Hash.InsertStatic(Handle, Pos, Radius);
				}
			}
			else
			{
				Hash.InsertDynamic(Handle, Pos, Radius);
			}
		});

		if (bRebuildStatic)
		{
			Hash.FinishStaticRebuild();
		}
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 5; }
	virtual FName GetSystemName() const override { return TEXT("CollisionBroadphase"); }
};
