/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCollisionResolutionSystem.h
 * @brief   PostTick system that runs one tick's full collider separation +
 *          overlap-event emission by delegating to the world's active
 *          USeinCollisionResolver.
 *
 *          This is a THIN DELEGATOR: it owns no resolution logic of its own. The
 *          actual Gauss-Seidel relaxation / mass-weighting / hard-barrier gate /
 *          overlap diff lives on the pluggable resolver (default
 *          USeinCollisionResolverDefault), chosen via
 *          `USeinARTSCoreSettings::CollisionResolverClass` and instantiated +
 *          owned by USeinWorldSubsystem. Swapping the resolver swaps the whole
 *          algorithm without touching this system, the registry, or the tick
 *          loop — the same pluggability pattern as Navigation / Fog-of-War.
 *
 *          Registration is unchanged from when this system held the logic inline:
 *          PostTick phase, SeinSystemPriority::CollisionResolution. It still runs
 *          after movement, so the resolver's settled separations are visible at
 *          the stable tick boundary and in deterministic snapshots.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinTickPhase.h"
#include "Core/SeinSystemPriority.h"
#include "Collision/SeinCollisionResolver.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/FixedPoint.h"

/**
 * System: Collision Resolution
 * Phase: PostTick | Priority: 10
 *
 * Delegates to World.GetCollisionResolver()->Resolve(World) once per tick. The
 * resolver does all the work; this system just sequences it into the tick loop
 * at the collision-resolution slot.
 */
class FSeinCollisionResolutionSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint /*DeltaTime*/, USeinWorldSubsystem& World) override
	{
		if (USeinCollisionResolver* Resolver = World.GetCollisionResolver())
		{
			Resolver->Resolve(World);
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.core.collision_resolution")),
			1u,
			ESeinTickPhase::PostTick,
			SeinSystemPriority::CollisionResolution);
	}
};
