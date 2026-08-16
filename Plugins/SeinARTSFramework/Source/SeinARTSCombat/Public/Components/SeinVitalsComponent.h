/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinVitalsComponent.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Deterministic vitals: health, armor class, regeneration.
 *
 *          Pure data — damage is applied through the combat module's
 *          deterministic resolution path (formula policy + ordered
 *          application), regeneration ticks in the weapon-cycle system, and
 *          death routes through the ordinary deferred entity teardown. An
 *          entity without this component is simply not damageable; nothing
 *          in the framework requires combat participation.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SeinComponent.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "SeinVitalsComponent.generated.h"

USTRUCT(BlueprintType, meta = (SeinDeterministic, DisplayName = "Vitals Component"))
struct SEINARTSCOMBAT_API FSeinVitalsComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Current health. Seeded from MaxHealth at spawn when authored <= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint Health = FFixedPoint::Zero;

	/** Health ceiling; regeneration and heals clamp here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint MaxHealth = FFixedPoint::FromInt(100);

	/** Armor-class tag handed to the damage formula (SeinARTS.Combat.Armor.*).
	 *  Classification metadata only — the formula decides what it means. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat",
		meta = (Categories = "SeinARTS.Combat.Armor"))
	FGameplayTag ArmorTag;

	/** Health regenerated per sim-second. Zero = none. Never revives — an
	 *  entity at zero is already in teardown. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint RegenPerSecond = FFixedPoint::Zero;

	/** Damage is refused entirely while set (scripted sequences, gameplay
	 *  invulnerability effects flip it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	bool bInvulnerable = false;

	/** Set by the cycle system after the one-time authored-health seed. Once
	 *  seeded, zero-or-below health ALWAYS means death — even when a scripted
	 *  whole-struct write zeroed it instead of the damage path — so an entity
	 *  can never silently re-seed back to full. Never author. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	bool bHealthSeeded = false;
};

FORCEINLINE uint32 GetTypeHash(const FSeinVitalsComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.Health);
	Hash = HashCombine(Hash, GetTypeHash(Component.MaxHealth));
	Hash = HashCombine(Hash, GetTypeHash(Component.ArmorTag));
	Hash = HashCombine(Hash, GetTypeHash(Component.RegenPerSecond));
	Hash = HashCombine(Hash, GetTypeHash(Component.bInvulnerable));
	Hash = HashCombine(Hash, GetTypeHash(Component.bHealthSeeded));
	return Hash;
}
