/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinProjectileSystem.h
 * @brief   Deterministic projectile flight: home on the live target (or its
 *          last known point), impact on arrival, lifetime fail-safe. Runs
 *          after the movement driver so shells chase this tick's settled
 *          target positions.
 */

#pragma once

#include "CoreMinimal.h"
#include "Combat/SeinCombatDamage.h"
#include "Components/SeinProjectileComponent.h"
#include "Core/SeinSystemPriority.h"
#include "Core/SeinTickPhase.h"
#include "Simulation/ComponentStorage.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

/**
 * System: Projectile Flight
 * Phase: AbilityExecution | Priority: 20 (after MovementDriver)
 */
class FSeinProjectileSystem final : public ISeinSystem
{
public:
	virtual void Tick(FFixedPoint DeltaTime, USeinWorldSubsystem& World) override
	{
		// Gather in canonical order, then fly — impacts destroy entities and
		// may splash other projectiles, so the sweep never mutates mid-walk.
		TArray<FSeinEntityHandle> Projectiles;
		if (const ISeinComponentStorage* Storage =
			World.GetComponentStorageRaw(
				FSeinProjectileComponent::StaticStruct()))
		{
			Storage->ForEachLiveComponent(
				[&](FSeinEntityHandle Handle, const void* /*Raw*/)
				{
					if (World.GetEntityPool().IsValid(Handle))
					{
						Projectiles.Add(Handle);
					}
				});
		}

		for (const FSeinEntityHandle& Handle : Projectiles)
		{
			if (!World.IsEntityAlive(Handle))
			{
				continue; // Intercepted or splashed earlier this sweep.
			}
			FSeinProjectileComponent* Flight =
				World.GetComponentMutable<FSeinProjectileComponent>(Handle);
			const FSeinEntity* Entity = World.GetEntity(Handle);
			if (!Flight || !Entity)
			{
				continue;
			}

			// Refresh homing while the target lives.
			if (Flight->Target.IsValid()
				&& World.IsEntityAlive(Flight->Target))
			{
				if (const FSeinEntity* TargetEntity =
					World.GetEntity(Flight->Target))
				{
					Flight->LastKnownTargetPoint =
						TargetEntity->Transform.GetLocation();
				}
			}
			else
			{
				Flight->Target = FSeinEntityHandle();
			}

			Flight->LifetimeRemaining =
				Flight->LifetimeRemaining - DeltaTime;

			const FFixedVector Position = Entity->Transform.GetLocation();
			const FFixedVector Destination = Flight->LastKnownTargetPoint;
			const FFixedPoint StepLength = Flight->Speed * DeltaTime;

			// Arrival: within this tick's travel of the destination.
			if (FFixedVector::IsDistanceWithin(
					Position, Destination, StepLength))
			{
				// Copy what the impact needs — resolution can reallocate
				// storage and invalidate Flight.
				const FSeinEntityHandle Target = Flight->Target;
				const FSeinEntityHandle Instigator = Flight->Instigator;
				const FSeinDamagePayload Payload = Flight->Payload;
				FSeinCombatDamage::ResolveImpact(
					World, Destination, Target, Instigator, Payload);
				World.DestroyEntity(Handle);
				continue;
			}
			if (Flight->LifetimeRemaining <= FFixedPoint::Zero)
			{
				// Never arrived (target outran lifetime) — expire without
				// an impact rather than detonating somewhere misleading.
				World.DestroyEntity(Handle);
				continue;
			}

			// Advance along the heading. GetSafeNormalDifference normalizes
			// without forming a wrapping delta (raw Size() wraps past ~463 m
			// — long artillery legs would alias otherwise).
			const FFixedVector Direction =
				FFixedVector::GetSafeNormalDifference(Position, Destination);
			FSeinEntity* MutableEntity =
				World.GetEntityMutable(Handle);
			if (MutableEntity)
			{
				MutableEntity->Transform.SetLocation(
					Position + Direction * StepLength);
			}
		}
	}

	virtual FSeinSystemDescriptor DescribeSystem() const override
	{
		return FSeinSystemDescriptor::Stateless(
			FName(TEXT("seinarts.combat.projectile_flight")),
			1u,
			ESeinTickPhase::AbilityExecution,
			SeinSystemPriority::ProjectileFlight);
	}
};
