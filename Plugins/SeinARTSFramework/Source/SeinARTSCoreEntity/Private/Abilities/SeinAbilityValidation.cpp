/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinAbilityValidation.cpp
 * @brief   Declarative target-validation implementation.
 */

#include "Abilities/SeinAbilityValidation.h"
#include "Abilities/SeinAbility.h"
#include "Components/SeinExtentsPayload.h"
#include "Components/SeinExtentsHelpers.h"
#include "Core/SeinEntityPool.h"
#include "Simulation/SeinWorldSubsystem.h"

ESeinAbilityTargetValidationResult FSeinAbilityValidation::ValidateTarget(
	const USeinAbility& Ability,
	FSeinEntityHandle Owner,
	FSeinEntityHandle Target,
	const FFixedVector& Location,
	USeinWorldSubsystem& World)
{
	// Range check — owner → nearest point on target's footprint surface
	// (NOT center). Matches the classic RTS "distance to the building, not
	// distance to the building's pivot" semantics. Build / repair / attack
	// abilities with a target that has FSeinExtentsPayload get face-aware
	// range; abilities targeting un-extents-ed entities (or world locations)
	// fall back to the classic owner-to-target-center check.
	//
	// Zero range means "unlimited," skip the check.
	if (Ability.MaxRange > FFixedPoint::Zero)
	{
		const FSeinEntity* OwnerEntity = World.GetEntity(Owner);
		if (OwnerEntity)
		{
			const FFixedVector OwnerLoc = OwnerEntity->Transform.GetLocation();
			FFixedVector TargetPos = Location;

			if (Target.IsValid())
			{
				if (const FSeinEntity* TargetEntity = World.GetEntity(Target))
				{
					TargetPos = TargetEntity->Transform.GetLocation();

					// Footprint-aware: if the target has extents, range
					// measures to the nearest point on the surface. Buffer=0
					// because we want true edge distance for the gate; a
					// non-zero buffer would just shrink effective range.
					if (const FSeinExtentsPayload* Extents = World.GetComponent<FSeinExtentsPayload>(Target))
					{
						if (Extents->Shapes.Num() > 0)
						{
							TargetPos = SeinExtentsHelpers::ComputeStandoffPoint(
								&Extents->Shapes[0],
								TargetEntity->Transform,
								OwnerLoc,
								FFixedPoint::Zero);
						}
					}
				}
			}

			const FFixedPoint DistSq = FFixedVector::DistSquared(OwnerLoc, TargetPos);
			const FFixedPoint RangeSq = Ability.MaxRange * Ability.MaxRange;
			if (DistSq > RangeSq)
			{
				return ESeinAbilityTargetValidationResult::OutOfRange;
			}
		}
	}

	// ValidTargetTags query — only meaningful when a target entity is specified.
	if (Target.IsValid() && !Ability.ValidTargetTags.IsEmpty())
	{
		if (!Ability.ValidTargetTags.Matches(World.GetEntityTags(Target)))
		{
			return ESeinAbilityTargetValidationResult::InvalidTarget;
		}
	}

	// Line-of-sight: consult the cross-module USeinWorldSubsystem::LineOfSightResolver
	// delegate, bound by USeinFogOfWarSubsystem at OnWorldBeginPlay. If unbound
	// (tests, fog-less games), LOS check trivially passes. Target position stays
	// FFixedVector end-to-end — no lossy FVector round-trip.
	if (Ability.bRequiresLineOfSight && World.LineOfSightResolver.IsBound())
	{
		const FSeinEntity* OwnerEntity = World.GetEntity(Owner);
		if (OwnerEntity)
		{
			const FSeinPlayerID OwnerPlayer = World.GetEntityOwner(Owner);
			FFixedVector TargetWorld = Location;
			if (Target.IsValid())
			{
				if (const FSeinEntity* TargetEntity = World.GetEntity(Target))
				{
					TargetWorld = TargetEntity->Transform.GetLocation();
				}
			}
			if (!World.LineOfSightResolver.Execute(OwnerPlayer, TargetWorld))
			{
				return ESeinAbilityTargetValidationResult::NoLineOfSight;
			}
		}
	}

	return ESeinAbilityTargetValidationResult::Valid;
}
