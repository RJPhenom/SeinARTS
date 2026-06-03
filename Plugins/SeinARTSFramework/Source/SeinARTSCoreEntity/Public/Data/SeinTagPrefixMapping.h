/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinTagPrefixMapping.h
 * @brief:   Settings row mapping an asset-name prefix (e.g. "SA") to a tag
 *           category segment (e.g. "Ability"). Drives auto-tag generation:
 *           an asset named `SA_Move` lands in the "Ability" category and
 *           produces tag `<TagPrefix>.Ability.Move`.
 *
 *           Plugin ships with SeinARTS conventions
 *           (SA→Ability, SU→Unit, SE→Effect, SR→Research, SBP→Entity).
 *           Downstream teams add their own (e.g. MA→Ability for MyGame
 *           naming) without removing the defaults; or replace defaults
 *           entirely.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinTagPrefixMapping.generated.h"

USTRUCT(BlueprintType)
struct SEINARTSCOREENTITY_API FSeinTagPrefixMapping
{
	GENERATED_BODY()

	/** Asset-name prefix, without the trailing underscore. Case-sensitive
	 *  match. Examples: "SA", "SU", "SE", "SR", "SBP", "MA", "MU". */
	UPROPERTY(Config, EditAnywhere, Category = "Tag Prefix Mapping")
	FString AssetPrefix;

	/** Tag category segment generated for assets with this prefix. Examples:
	 *  "Ability", "Unit", "Effect", "Research", "Entity". Combined with the
	 *  global TagPrefix to form the tag root (e.g. "SeinARTS.Ability"). */
	UPROPERTY(Config, EditAnywhere, Category = "Tag Prefix Mapping")
	FString TagCategory;

	FSeinTagPrefixMapping() = default;
	FSeinTagPrefixMapping(const FString& InAssetPrefix, const FString& InTagCategory)
		: AssetPrefix(InAssetPrefix), TagCategory(InTagCategory) {}
};
