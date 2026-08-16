/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinWeaponFire.cpp
 * @brief   Fire gate: readiness + legality validation, delivery dispatch,
 *          cycle-timer start.
 */

#include "Combat/SeinWeaponFire.h"
#include "Combat/SeinCombatDamage.h"
#include "Components/SeinProjectileComponent.h"
#include "Components/SeinVitalsComponent.h"
#include "Components/SeinWeaponComponent.h"
#include "Math/MathLib.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Types/Entity.h"

namespace
{
	FFixedPoint PlanarDistanceSaturated(
		const FFixedVector& A, const FFixedVector& B)
	{
		FFixedVector PlanarA = A;
		FFixedVector PlanarB = B;
		PlanarA.Z = FFixedPoint::Zero;
		PlanarB.Z = FFixedPoint::Zero;
		return FFixedVector::DistanceSaturated(PlanarA, PlanarB);
	}

	bool IsSlotReady(const FSeinWeaponSlot& Slot)
	{
		return Slot.CooldownRemaining <= FFixedPoint::Zero
			&& Slot.ReloadRemaining <= FFixedPoint::Zero
			&& (Slot.MagazineSize <= 0 || Slot.MagazineRemaining > 0);
	}
}

bool FSeinWeaponFire::IsWeaponReady(
	const USeinWorldSubsystem& World,
	FSeinEntityHandle Shooter,
	int32 WeaponIndex)
{
	const FSeinWeaponComponent* Weapons =
		World.GetComponent<FSeinWeaponComponent>(Shooter);
	return Weapons
		&& Weapons->Weapons.IsValidIndex(WeaponIndex)
		&& IsSlotReady(Weapons->Weapons[WeaponIndex]);
}

ESeinWeaponFireResult FSeinWeaponFire::TryFireWeaponAt(
	USeinWorldSubsystem& World,
	FSeinEntityHandle Shooter,
	int32 WeaponIndex,
	FSeinEntityHandle Target)
{
	if (!World.IsEntityAlive(Shooter))
	{
		return ESeinWeaponFireResult::InvalidShooter;
	}
	const FSeinWeaponComponent* Weapons =
		World.GetComponent<FSeinWeaponComponent>(Shooter);
	if (!Weapons || !Weapons->Weapons.IsValidIndex(WeaponIndex))
	{
		return ESeinWeaponFireResult::InvalidWeaponIndex;
	}
	// Legality reads the authored slot by value — storage may reallocate
	// when the projectile spawn below adds components.
	const FSeinWeaponSlot Slot = Weapons->Weapons[WeaponIndex];
	if (!IsSlotReady(Slot))
	{
		return ESeinWeaponFireResult::NotReady;
	}
	if (!World.IsEntityAlive(Target)
		|| !World.GetComponent<FSeinVitalsComponent>(Target))
	{
		return ESeinWeaponFireResult::InvalidTarget;
	}

	const FSeinEntity* ShooterEntity = World.GetEntity(Shooter);
	const FSeinEntity* TargetEntity = World.GetEntity(Target);
	if (!ShooterEntity || !TargetEntity)
	{
		return ESeinWeaponFireResult::InvalidTarget;
	}
	const FFixedVector ShooterLocation =
		ShooterEntity->Transform.GetLocation();
	const FFixedVector TargetLocation =
		TargetEntity->Transform.GetLocation();
	if (!FFixedVector::IsPlanarDistanceWithin(
			ShooterLocation, TargetLocation, Slot.Range))
	{
		return ESeinWeaponFireResult::OutOfRange;
	}
	if (Slot.ArcHalfAngleDegrees < FFixedPoint::FromInt(180))
	{
		FFixedVector PlanarForward =
			ShooterEntity->Transform.GetRotation().GetForwardVector();
		PlanarForward.Z = FFixedPoint::Zero;
		FFixedVector PlanarDelta = TargetLocation - ShooterLocation;
		PlanarDelta.Z = FFixedPoint::Zero;
		const FFixedPoint Distance =
			PlanarDistanceSaturated(TargetLocation, ShooterLocation);
		const FFixedPoint CosHalfAngle = SeinMath::Cos(
			Slot.ArcHalfAngleDegrees
			* FFixedPoint::Pi / FFixedPoint::FromInt(180));
		const FFixedPoint Dot =
			PlanarForward.X * PlanarDelta.X
			+ PlanarForward.Y * PlanarDelta.Y;
		if (Dot < Distance * CosHalfAngle)
		{
			return ESeinWeaponFireResult::OutsideArc;
		}
	}
	if (Slot.bRequireLineOfSight
		&& World.LineOfSightResolver.IsBound()
		&& !World.LineOfSightResolver.Execute(
			World.GetEntityOwner(Shooter), TargetLocation))
	{
		return ESeinWeaponFireResult::NoLineOfSight;
	}

	// ── Delivery ──
	if (Slot.Delivery == ESeinWeaponDelivery::Instant)
	{
		FSeinCombatDamage::ResolveImpact(
			World, TargetLocation, Target, Shooter, Slot.Payload);
	}
	else
	{
		// Spawn the projectile entity at the shooter, facing the target.
		FFixedTransform SpawnTransform(ShooterLocation);
		UClass* ProjectileClass = Slot.ProjectileClass.IsNull()
			? nullptr
			: Slot.ProjectileClass.LoadSynchronous();
		const FSeinEntityHandle Projectile = ProjectileClass
			? World.SpawnEntity(ProjectileClass, SpawnTransform,
				World.GetEntityOwner(Shooter))
			: World.SpawnAbstractEntity(SpawnTransform,
				World.GetEntityOwner(Shooter));
		if (Projectile.IsValid())
		{
			FSeinProjectileComponent Flight;
			Flight.Instigator = Shooter;
			Flight.Target = Target;
			Flight.LastKnownTargetPoint = TargetLocation;
			Flight.Speed = Slot.ProjectileSpeed > FFixedPoint::Zero
				? Slot.ProjectileSpeed
				: FFixedPoint::FromInt(2000);
			Flight.Payload = Slot.Payload;
			World.AddComponent(Projectile, Flight);
		}
	}

	// ── Cycle ── (mutable re-fetch: delivery may have reallocated storage)
	FSeinWeaponComponent* MutableWeapons =
		World.GetComponentMutable<FSeinWeaponComponent>(Shooter);
	if (MutableWeapons
		&& MutableWeapons->Weapons.IsValidIndex(WeaponIndex))
	{
		FSeinWeaponSlot& MutableSlot =
			MutableWeapons->Weapons[WeaponIndex];
		MutableSlot.CooldownRemaining = MutableSlot.CooldownSeconds;
		if (MutableSlot.MagazineSize > 0)
		{
			--MutableSlot.MagazineRemaining;
			if (MutableSlot.MagazineRemaining <= 0)
			{
				MutableSlot.ReloadRemaining = MutableSlot.ReloadSeconds;
			}
		}
	}
	return ESeinWeaponFireResult::Fired;
}
