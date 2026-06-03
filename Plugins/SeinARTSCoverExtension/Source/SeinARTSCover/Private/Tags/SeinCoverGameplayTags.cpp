/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverGameplayTags.cpp
 */

#include "Tags/SeinCoverGameplayTags.h"

namespace SeinCoverTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cover,          "SeinARTS.Cover",          "Root tag for cover quality tags");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cover_Heavy,    "SeinARTS.Cover.Heavy",    "Heavy cover — sandbags, walls, building corners. Major damage reduction.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cover_Light,    "SeinARTS.Cover.Light",    "Light cover — fences, craters, low fences. Modest damage reduction.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cover_Negative, "SeinARTS.Cover.Negative", "Negative cover — roads, exposed lanes. Increased damage taken.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cover_UsesCover, "SeinARTS.Cover.UsesCover", "Entity-class eligibility tag — entity is a cover CONSUMER (cover-aware broker resolvers snap it to nearby slots when moving). Infantry yes, vehicles no.");
}
