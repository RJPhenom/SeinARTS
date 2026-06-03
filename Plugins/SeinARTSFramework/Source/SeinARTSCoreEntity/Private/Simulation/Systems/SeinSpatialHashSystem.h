/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinSpatialHashSystem.h
 * @brief   PreTick system that rebuilds the world's spatial hash from current
 *          entity positions. Foundation for avoidance, proximity queries, and
 *          any future cross-cutting "find me units near here" logic.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Simulation/SeinSpatialHash.h"
#include "Components/SeinNavigationComponent.h"
#include "Components/SeinExtentsComponent.h"
#include "Components/SeinExtentsHelpers.h"  // BoundingRadius — matches collision/penetration cascade

/**
 * System: Spatial Hash Rebuild
 * Phase: PreTick | Priority: 5
 *
 * Runs after EffectTick (priority 0) so any tick-time spawn/destroy from
 * effects has already settled. Walks the entity pool in handle-index order,
 * inserts every entity that has an FSeinNavigationComponent with
 * FallbackFootprintRadius > 0 into the hash. Entities without nav data,
 * dead entities, and intangible entities (FallbackFootprintRadius == 0)
 * are skipped.
 *
 * Note: this is a gating check, not a "use this size" lookup. The actual
 * collision footprint at runtime cascades Extents → NavComp via
 * USeinMovement::ResolveCollisionRadius. The intent here is "skip
 * entities the designer explicitly opted out of collision for."
 *
 * Full clear+rebuild each tick. Cheap at sim scale (a few thousand inserts)
 * and obviates any state-tracking. Incremental updates are a Phase E
 * optimization in the avoidance plan.
 */
class FSeinSpatialHashSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		FSeinSpatialHash& Hash = World.GetSpatialHash();
		Hash.Clear();

		World.GetEntityPool().ForEachEntity([&](FSeinEntityHandle Handle, FSeinEntity& Entity)
		{
			// Gate: include only entities with a non-zero effective collision
			// radius via the shared cascade (Extents → NavComp fallback).
			// Without checking Extents, a tank with Extents but NavComp
			// FallbackFootprintRadius=0 would be invisible to penetration /
			// avoidance queries — the spatial hash gate must agree with the
			// rest of the system on what counts as "has a body."
			const FSeinNavigationComponent* NavData = World.GetComponent<FSeinNavigationComponent>(Handle);
			if (!NavData) return;
			FFixedPoint EffectiveRadius = FFixedPoint::Zero;
			if (const FSeinExtentsComponent* Extents = World.GetComponent<FSeinExtentsComponent>(Handle))
			{
				for (const FSeinExtentsShape& Shape : Extents->Shapes)
				{
					const FFixedPoint R = SeinExtentsHelpers::BoundingRadius(Shape);
					if (R > EffectiveRadius) EffectiveRadius = R;
				}
			}
			if (EffectiveRadius <= FFixedPoint::Zero)
			{
				EffectiveRadius = NavData->FallbackFootprintRadius;
			}
			if (EffectiveRadius <= FFixedPoint::Zero) return;
			Hash.Insert(Handle, Entity.Transform.GetLocation());
		});
	}

	virtual ESeinTickPhase GetPhase() const override { return ESeinTickPhase::PreTick; }
	virtual int32 GetPriority() const override { return 5; }
	virtual FName GetSystemName() const override { return TEXT("SpatialHash"); }
};
