/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinWeaponFire.h
 * @author       RJ Macklem
 * @created      16 Aug 2026
 * @brief        The deterministic fire gate + delivery dispatch.
 *
 *          One sanctioned path from "an ability decided to shoot" to a
 *          resolved hit or a spawned projectile: validate readiness (cooldown,
 *          reload, magazine), legality (alive target with vitals, range, arc,
 *          fog LoS), execute the slot's delivery, and start the cycle timers.
 *          Abilities call this through the restricted combat library; nothing
 *          in the framework decides WHEN to call it.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"

class USeinWorldSubsystem;

/** Why a fire attempt was refused (Fired = success). */
enum class ESeinWeaponFireResult : uint8
{
	Fired,
	InvalidShooter,
	InvalidWeaponIndex,
	NotReady,
	InvalidTarget,
	OutOfRange,
	OutsideArc,
	NoLineOfSight,
};

class SEINARTSCOMBAT_API FSeinWeaponFire
{
public:
	/** Attempt to fire one authored weapon slot at a target entity. */
	static ESeinWeaponFireResult TryFireWeaponAt(
		USeinWorldSubsystem& World,
		FSeinEntityHandle Shooter,
		int32 WeaponIndex,
		FSeinEntityHandle Target);

	/** Readiness-only probe (timers + magazine), no legality checks — lets
	 *  abilities early-out before running acquisition. */
	static bool IsWeaponReady(
		const USeinWorldSubsystem& World,
		FSeinEntityHandle Shooter,
		int32 WeaponIndex);

private:
	FSeinWeaponFire() = delete;
};
