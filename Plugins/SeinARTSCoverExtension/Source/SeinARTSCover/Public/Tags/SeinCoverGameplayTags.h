/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverGameplayTags.h
 * @brief   Framework-shipped cover quality tags. Designers can still author
 *          their own (the cover system uses arbitrary FGameplayTags, not an
 *          enum) — these three are the canonical defaults the framework + UI
 *          recognize out of the box.
 *
 *          Cover-specific tags live in this module rather than in
 *          SeinARTSGameplayTags so they're only registered when the optional
 *          cover module is loaded.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"

namespace SeinCoverTags
{
	// --- Cover root ---
	SEINARTSCOVER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cover);

	// --- Cover qualities ---
	// Designers stamp one of these on FSeinCoverComponent::QualityTag (one
	// tag per provider, applied to all of that provider's slots + area volume)
	// to drive the preview decal color and combat damage modifiers. Extend in
	// project tags with project-specific sub-tags as needed (e.g.
	// SeinARTS.Cover.Heavy.Concrete) — the canonical three are recognized
	// directly by the framework's default priority ordering.
	SEINARTSCOVER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cover_Heavy);
	SEINARTSCOVER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cover_Light);
	SEINARTSCOVER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cover_Negative);

	// --- Cover eligibility ---
	// Designer tags an entity class's BaseTags with this to mark the entity as
	// a cover CONSUMER — eligible for cover-snap when receiving move orders
	// via a cover-aware broker resolver. Infantry: tag it. Vehicles,
	// aircraft, buildings: leave it off. Pure opt-in — entities without the
	// tag get default formation positions regardless of nearby cover.
	//
	// Why a tag instead of a bool on the entity class: keeps the cover module
	// fully decoupled from SeinARTSCoreEntity. Entity class carries an arbitrary
	// BaseTags container; we just reserve a specific tag for the cover
	// behavior. Disable the cover module → tag isn't registered, no entity class
	// can claim it, the cover-aware resolvers aren't loaded — clean opt-in.
	SEINARTSCOVER_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cover_UsesCover);
}
