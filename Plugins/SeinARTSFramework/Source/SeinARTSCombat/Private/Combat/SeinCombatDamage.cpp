/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatDamage.cpp
 * @brief   Deterministic damage application: formula policy resolution,
 *          ordered splash gathering, vitals mutation, death teardown, and
 *          the damage/death/kill visual events.
 */

#include "Combat/SeinCombatDamage.h"
#include "Combat/SeinCombatMath.h"
#include "Combat/SeinDamageFormula.h"
#include "Components/SeinVitalsComponent.h"
#include "Events/SeinVisualEvent.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "Tags/SeinCombatGameplayTags.h"
#include "Types/Entity.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinCombat, Log, All);

namespace
{
	/** Resolve the payload's formula policy CDO; null path = flat built-in. */
	const USeinDamageFormula* ResolveFormula(const FSeinDamagePayload& Payload)
	{
		if (Payload.FormulaClass.IsValid())
		{
			if (UClass* FormulaClass =
				Payload.FormulaClass.TryLoadClass<USeinDamageFormula>())
			{
				return GetDefault<USeinDamageFormula>(FormulaClass);
			}
			// An authored-but-unresolvable class is a content error worth
			// hearing about once per payload use, not a silent flat fallback.
			UE_LOG(LogSeinCombat, Warning,
				TEXT("Damage payload formula class %s did not resolve; using the flat built-in."),
				*Payload.FormulaClass.ToString());
		}
		return nullptr;
	}
}

FFixedPoint FSeinCombatDamage::ApplyDamage(
	USeinWorldSubsystem& World,
	FSeinEntityHandle Target,
	FSeinEntityHandle Instigator,
	const FSeinDamagePayload& Payload,
	FFixedPoint DistanceFromImpact)
{
	if (!World.IsEntityAlive(Target))
	{
		return FFixedPoint::Zero;
	}
	FSeinVitalsComponent* Vitals =
		World.GetComponentMutable<FSeinVitalsComponent>(Target);
	if (!Vitals || Vitals->bInvulnerable
		|| Vitals->Health <= FFixedPoint::Zero)
	{
		return FFixedPoint::Zero;
	}

	FSeinDamageContext Context;
	Context.Target = Target;
	Context.Instigator = Instigator;
	Context.Payload = Payload;
	Context.TargetArmorTag = Vitals->ArmorTag.IsValid()
		? Vitals->ArmorTag
		: SeinCombatTags::Combat_Armor_None;
	Context.DistanceFromImpact = DistanceFromImpact;

	const USeinDamageFormula* Formula = ResolveFormula(Payload);
	FFixedPoint Damage = Formula
		? Formula->ComputeDamage(Context)
		: Payload.BaseDamage;
	if (Damage < FFixedPoint::Zero)
	{
		Damage = FFixedPoint::Zero;
	}
	// A formula may legitimately zero out a hit (immune armor class).
	if (Damage == FFixedPoint::Zero)
	{
		return FFixedPoint::Zero;
	}

	// The formula's CDO evaluation may not mutate storage, but re-fetch
	// defensively — Blueprint formulas can call component readers whose
	// storage lookups must never be assumed pointer-stable.
	Vitals = World.GetComponentMutable<FSeinVitalsComponent>(Target);
	if (!Vitals)
	{
		return FFixedPoint::Zero;
	}
	const FFixedPoint Dealt =
		Damage < Vitals->Health ? Damage : Vitals->Health;
	Vitals->Health = Vitals->Health - Dealt;

	const FGameplayTag DamageTypeTag = Payload.DamageTypeTag.IsValid()
		? Payload.DamageTypeTag
		: SeinCombatTags::Combat_Damage_Default;
	World.EnqueueVisualEvent(FSeinVisualEvent::MakeDamageAppliedEvent(
		Target, Instigator, Dealt, DamageTypeTag));

	if (Vitals->Health <= FFixedPoint::Zero)
	{
		World.EnqueueVisualEvent(
			FSeinVisualEvent::MakeDeathEvent(Target, Instigator));
		if (Instigator.IsValid())
		{
			World.EnqueueVisualEvent(FSeinVisualEvent::MakeKillEvent(
				Instigator, Target, World.GetEntityOwner(Instigator)));
		}
		// Ordinary deferred teardown — brokers, containment, squads, and
		// reservations all settle through the existing sweep.
		World.DestroyEntity(Target);
	}
	return Dealt;
}

int32 FSeinCombatDamage::ResolveImpact(
	USeinWorldSubsystem& World,
	const FFixedVector& ImpactPoint,
	FSeinEntityHandle DirectTarget,
	FSeinEntityHandle Instigator,
	const FSeinDamagePayload& Payload)
{
	int32 Victims = 0;
	if (Payload.AreaRadius <= FFixedPoint::Zero)
	{
		if (ApplyDamage(World, DirectTarget, Instigator, Payload)
			> FFixedPoint::Zero)
		{
			++Victims;
		}
		return Victims;
	}

	// Gather victims FIRST in canonical ascending-slot order, then apply —
	// application can destroy entities, and mutating the pool mid-iteration
	// would make victim order depend on teardown side effects.
	struct FSplashVictim
	{
		FSeinEntityHandle Handle;
		FFixedPoint Distance;
	};
	TArray<FSplashVictim> SplashVictims;
	World.GetEntityPool().ForEachEntity(
		[&](FSeinEntityHandle Handle, const FSeinEntity& Entity)
		{
			if (Handle == DirectTarget)
			{
				return; // Always evaluated at distance zero below.
			}
			if (!World.GetComponent<FSeinVitalsComponent>(Handle))
			{
				return;
			}
			const FFixedVector Location = Entity.Transform.GetLocation();
			if (!FFixedVector::IsPlanarDistanceWithin(
					Location, ImpactPoint, Payload.AreaRadius))
			{
				return;
			}
			SplashVictims.Add({Handle,
				SeinCombatInternal::PlanarDistanceSaturated(
					Location, ImpactPoint)});
		});

	if (World.IsEntityAlive(DirectTarget)
		&& ApplyDamage(World, DirectTarget, Instigator, Payload)
			> FFixedPoint::Zero)
	{
		++Victims;
	}
	for (const FSplashVictim& Victim : SplashVictims)
	{
		if (ApplyDamage(World, Victim.Handle, Instigator, Payload,
				Victim.Distance) > FFixedPoint::Zero)
		{
			++Victims;
		}
	}
	return Victims;
}
