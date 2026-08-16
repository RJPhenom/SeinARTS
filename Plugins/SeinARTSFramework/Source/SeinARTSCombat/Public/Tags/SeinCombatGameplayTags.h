/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatGameplayTags.h
 * @brief   Native gameplay tags owned by the combat module. Damage-type and
 *          armor-class catalogs are deliberately tiny — games author their own
 *          tags under these roots; the defaults exist so the starter content
 *          works out of the box.
 */

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

namespace SeinCombatTags
{
	/** Root for damage-type tags (SeinARTS.Combat.Damage.*). */
	SEINARTSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Damage);
	/** Neutral default damage type used by the starter weapon. */
	SEINARTSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Damage_Default);

	/** Root for armor-class tags (SeinARTS.Combat.Armor.*). */
	SEINARTSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Armor);
	/** Unarmored default. */
	SEINARTSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Combat_Armor_None);
}
