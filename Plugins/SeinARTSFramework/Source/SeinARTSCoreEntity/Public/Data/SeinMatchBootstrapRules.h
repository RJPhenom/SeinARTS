/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMatchBootstrapRules.h
 * @brief   Optional deterministic rules consumed while materializing tick zero.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "SeinMatchBootstrapRules.generated.h"

/** One exact match-level override applied after catalog and faction defaults. */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinStartingResourceOverride
{
	GENERATED_BODY()

	/** Resource catalog entry to override for every active, non-neutral player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Match|Resources",
		meta = (Categories = "SeinARTS.Resource"))
	FGameplayTag ResourceTag;

	/** Exact starting balance installed after the faction ResourceKit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Match|Resources")
	FFixedPoint Amount = FFixedPoint::Zero;
};

/**
 * Framework-provided tick-zero rules. Add this value to
 * `FSeinMatchSettings::Extensions` when a match needs global starting-resource
 * overrides. Faction-specific starts remain authored in each faction's
 * ResourceKit; game-specific handicaps can use their own match extension.
 *
 * The array is canonicalized by resource-tag text before the match contract is
 * digested. Duplicate or unknown resource tags fail match validation before
 * any player or entity state is allocated.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinMatchBootstrapRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Match|Resources")
	TArray<FSeinStartingResourceOverride> StartingResources;
};
