/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinWeaponComponent.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        Authored weapon slots + deterministic cycling state.
 *
 *          A weapon slot is clockwork: range/arc gates, a cooldown, an
 *          optional magazine with a reload, and a delivery kind with its
 *          damage payload. WHO fires and AT WHAT is never decided here —
 *          abilities (the starter Attack/auto-acquire content or a game's
 *          own) call Fire Weapon through the restricted combat library, and
 *          the weapon-cycle system only advances timers. Pure data component;
 *          runtime fields are BlueprintReadOnly canonical state.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Actor/SeinActor.h"
#include "Combat/SeinCombatTypes.h"
#include "Components/SeinComponent.h"
#include "Types/FixedPoint.h"
#include "UObject/SoftObjectPtr.h"
#include "SeinWeaponComponent.generated.h"

/** One authored weapon plus its deterministic cycling state. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOMBAT_API FSeinWeaponSlot
{
	GENERATED_BODY()

	// ─── Authored ───

	/** Maximum planar firing range (world units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint Range = FFixedPoint::FromInt(1000);

	/** Firing-arc half-angle around the entity's facing, in degrees.
	 *  180 (default) = fires in any direction. Turret slewing is authored
	 *  with child transforms + the Turn Child Toward node; this gate only
	 *  decides whether fire is currently legal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat",
		meta = (ClampMin = "0", ClampMax = "180"))
	FFixedPoint ArcHalfAngleDegrees = FFixedPoint::FromInt(180);

	/** Seconds between shots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint CooldownSeconds = FFixedPoint::One;

	/** Shots before a reload. Zero (default) = no magazine, cooldown only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat",
		meta = (ClampMin = "0"))
	int32 MagazineSize = 0;

	/** Reload duration once the magazine empties. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint ReloadSeconds = FFixedPoint::Zero;

	/** Gate fire through the fog line-of-sight resolver when bound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	bool bRequireLineOfSight = true;

	/** How fire reaches the target (see ESeinWeaponDelivery). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	ESeinWeaponDelivery Delivery = ESeinWeaponDelivery::Instant;

	/** Projectile delivery only: flight speed (world units / second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FFixedPoint ProjectileSpeed = FFixedPoint::FromInt(2000);

	/** Projectile delivery only: optional unit Blueprint for the projectile
	 *  entity (visuals/extra components). Empty spawns an abstract sim-only
	 *  projectile — correct but invisible; games set this for tracers/shells
	 *  they want rendered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	TSoftClassPtr<ASeinActor> ProjectileClass;

	/** What a hit delivers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	FSeinDamagePayload Payload;

	// ─── Runtime (canonical cycling state; never author) ───

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedPoint CooldownRemaining = FFixedPoint::Zero;

	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	FFixedPoint ReloadRemaining = FFixedPoint::Zero;

	/** Shots left in the magazine. Seeded to MagazineSize on first tick when
	 *  a magazine is authored. Meaningless while MagazineSize is zero. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	int32 MagazineRemaining = 0;
};

USTRUCT(BlueprintType, meta = (SeinDeterministic, DisplayName = "Weapon Component"))
struct SEINARTSCOMBAT_API FSeinWeaponComponent : public FSeinComponent
{
	GENERATED_BODY()

	/** Authored weapons. Slot index is the stable identity abilities fire by. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Combat")
	TArray<FSeinWeaponSlot> Weapons;

	/** True once the cycle system seeded runtime state (magazines). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Combat")
	bool bRuntimeSeeded = false;
};

FORCEINLINE uint32 GetTypeHash(const FSeinWeaponSlot& Slot)
{
	uint32 Hash = GetTypeHash(Slot.Range);
	Hash = HashCombine(Hash, GetTypeHash(Slot.CooldownSeconds));
	Hash = HashCombine(Hash, GetTypeHash(Slot.MagazineSize));
	Hash = HashCombine(Hash, GetTypeHash(Slot.CooldownRemaining));
	Hash = HashCombine(Hash, GetTypeHash(Slot.ReloadRemaining));
	Hash = HashCombine(Hash, GetTypeHash(Slot.MagazineRemaining));
	return Hash;
}

FORCEINLINE uint32 GetTypeHash(const FSeinWeaponComponent& Component)
{
	uint32 Hash = GetTypeHash(Component.bRuntimeSeeded);
	Hash = HashCombine(Hash, GetTypeHash(Component.Weapons.Num()));
	for (const FSeinWeaponSlot& Slot : Component.Weapons)
	{
		Hash = HashCombine(Hash, GetTypeHash(Slot));
	}
	return Hash;
}
