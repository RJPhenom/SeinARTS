/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCombatGameplayTags.cpp
 * @brief   Native tag definitions for the combat module.
 */

#include "Tags/SeinCombatGameplayTags.h"

namespace SeinCombatTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Combat_Damage, "SeinARTS.Combat.Damage", "Root for damage-type tags; games author their own children");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Combat_Damage_Default, "SeinARTS.Combat.Damage.Default", "Neutral default damage type used by starter content");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Combat_Armor, "SeinARTS.Combat.Armor", "Root for armor-class tags; games author their own children");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Combat_Armor_None, "SeinARTS.Combat.Armor.None", "Unarmored default armor class");
}
