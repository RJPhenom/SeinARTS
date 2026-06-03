/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverBPFL.h
 * @brief   Blueprint surface for the cover system. Thin wrappers around
 *          USeinCoverSubsystem::GetCoverSystem() that designers can call from
 *          combat ability scripts, damage formulas, UI, etc.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinEntityHandle.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Types/SeinCoverTypes.h"
#include "SeinCoverBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Cover Library"))
class SEINARTSCOVER_API USeinCoverBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns every cover context active at the given world point. Empty
	 *  array = no cover. Used by combat ability scripts to apply per-cover
	 *  modifiers — iterate the array and evaluate each context against the
	 *  shot's incoming direction. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Cover",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Query Cover At"))
	static TArray<FSeinCoverContext> SeinQueryCoverAt(
		const UObject* WorldContextObject,
		FFixedVector WorldPoint);

	/** Returns the strongest cover quality tag at the given world point, or
	 *  an invalid tag when there's no cover. Convenience for UI / preview
	 *  decals that need a single representative tag per point. Combat scripts
	 *  should iterate the full array via SeinQueryCoverAt instead. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Cover",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Query Best Cover Quality At"))
	static FGameplayTag SeinQueryBestCoverQualityAt(
		const UObject* WorldContextObject,
		FFixedVector WorldPoint);

	/** Returns the unit outward vector from the cover provider's SeinExtents
	 *  body to the entity-in-cover's world position. Combat damage formulas
	 *  call this when an `FSeinCoverContext::bIsDirectional` is true; the
	 *  return value is meant to be dotted against the shot's incoming-FROM
	 *  direction:
	 *    +1 = shot from same outward direction as cover-body→entity (fully
	 *         covered — wall is between unit and shooter)
	 *    -1 = shot from opposite direction (fully flanked — wall is behind
	 *         the unit relative to the shooter)
	 *    smooth interpolation between for partial angles
	 *
	 *  Designers map the dot to a damage multiplier however the project's
	 *  curve dictates. Zero vector return = provider has no SeinExtents
	 *  body, or the entity is exactly on the body's surface — caller should
	 *  treat as "no direction info available" (typically apply the full
	 *  quality modifier without direction modulation). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Cover",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Cover Direction"))
	static FFixedVector SeinGetCoverDirection(
		const UObject* WorldContextObject,
		FFixedVector EntityWorldPosition,
		FSeinEntityHandle ProviderHandle);
};
