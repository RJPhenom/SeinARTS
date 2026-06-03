/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceTypes.h
 * @brief   Economy primitives: resource catalog entries, faction kits, unified
 *          cost struct, with cost-direction and production-deduction
 *          timing enums.
 *
 *          Resources are identified by FGameplayTag under the SeinARTS.Resource
 *          namespace. Designers declare resources in
 *          USeinARTSCoreSettings::ResourceCatalog and opt each faction in to
 *          them via USeinFaction::ResourceKit.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "SeinResourceTypes.generated.h"

class UTexture2D;

// =====================================================================
// Enums
// =====================================================================

/**
 * What a resource's balance does when income would exceed its cap.
 * Configured per-resource on its catalog entry.
 */
UENUM(BlueprintType)
enum class ESeinResourceOverflowBehavior : uint8
{
	/** Income beyond cap is discarded (default — classic stockpile). */
	ClampAtCap,
	/** Allow the balance to grow unbounded; cap is advisory-only. */
	AllowUnbounded,
	/** Overflow is redirected (e.g. to a score or secondary resource). Designer
	 *  supplies the resolver behavior via a custom handler elsewhere. */
	RedirectOverflow
};

/**
 * What a resource does when a cost or drain would put the balance below zero.
 */
UENUM(BlueprintType)
enum class ESeinResourceSpendBehavior : uint8
{
	/** Hard reject — cannot afford, operation fails (default stockpile). */
	RejectOnInsufficient,
	/** Allow the balance to go negative (upkeep-style debt economies). */
	AllowDebt,
	/** Custom resolver hook — designer supplies the reject/allow decision. */
	CustomResolver
};

/**
 * DESIGN §9 Q5. How a cost's amount is applied against the resource balance.
 * Lets one resource represent classic stockpile spend (manpower) while another
 * represents pop / supply cap (balance grows toward cap on spend).
 */
UENUM(BlueprintType)
enum class ESeinCostDirection : uint8
{
	/** Cost is subtracted from the balance. Affordability: Balance >= Cost. */
	DeductFromBalance,
	/** Cost is added to the balance, capped by the resource's Cap. Affordability:
	 *  Balance + Cost <= Cap. Used for pop/supply and similar cap-bound resources. */
	AddTowardCap
};

/**
 * DESIGN §9 Q1/Q6. When a production cost commits against the owning player.
 * Declared on the resource's catalog entry — applies to every production cost
 * entry that uses that resource tag.
 */
UENUM(BlueprintType)
enum class ESeinProductionDeductionTiming : uint8
{
	/** Cost commits the moment the item is enqueued. Default for stockpile
	 *  resources; prevents over-queueing beyond the owning player's balance. */
	AtEnqueue,
	/** Cost is only attempted when the built entity tries to spawn. Allows
	 *  over-queueing; the queue can stall at 100% if the AtCompletion cost
	 *  would exceed cap. Default for cap-bound resources (pop, supply). */
	AtCompletion
};

// =====================================================================
// FSeinResourceDefinition
// =====================================================================

/**
 * Project-wide declaration of a single resource. Lives on
 * USeinARTSCoreSettings::ResourceCatalog. Factions reference these by tag in
 * their ResourceKit, optionally supplying overrides.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinResourceDefinition
{
	GENERATED_BODY()

	/** Gameplay tag identifying this resource (must live under SeinARTS.Resource.*). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy",
		meta = (Categories = "SeinARTS.Resource"))
	FGameplayTag ResourceTag;

	/** Display name for UI (tooltips, resource bars). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	FText DisplayName;

	/** Icon used in UI displays (resource bar, cost tooltips). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	TObjectPtr<UTexture2D> Icon;

	/** Default starting balance for players of factions that use this resource,
	 *  unless the faction's kit supplies an override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	FFixedPoint DefaultStartingValue = FFixedPoint::Zero;

	/** Default cap (maximum balance). FFixedPoint::Zero means "uncapped" for
	 *  resources with ClampAtCap overflow; for AddTowardCap resources this is
	 *  the real cap — balance cannot exceed it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	FFixedPoint DefaultCap = FFixedPoint::Zero;

	/** How overflow is handled when income would exceed the cap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	ESeinResourceOverflowBehavior OverflowBehavior = ESeinResourceOverflowBehavior::ClampAtCap;

	/** How insufficient-balance spends are handled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	ESeinResourceSpendBehavior SpendBehavior = ESeinResourceSpendBehavior::RejectOnInsufficient;

	/** How cost entries against this resource are applied (deduct vs add-toward-cap).
	 *  DESIGN §9 Q5. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	ESeinCostDirection CostDirection = ESeinCostDirection::DeductFromBalance;

	/** When production costs against this resource commit. DESIGN §9 Q1/Q6. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	ESeinProductionDeductionTiming ProductionDeductionTiming = ESeinProductionDeductionTiming::AtEnqueue;
};

// =====================================================================
// FSeinFactionResourceEntry
// =====================================================================

/**
 * A single entry in a faction's ResourceKit. References a catalog resource by
 * tag and optionally overrides the default starting value / cap for players
 * of that faction. Any catalog entry with a matching tag is opted in; catalog
 * entries without a matching kit entry are not usable by this faction.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinFactionResourceEntry
{
	GENERATED_BODY()

	/** Tag of the catalog entry this kit slot opts into. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy",
		meta = (Categories = "SeinARTS.Resource"))
	FGameplayTag ResourceTag;

	/** If true, override the catalog's default starting value with StartingValueOverride. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	bool bOverrideStartingValue = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy",
		meta = (EditCondition = "bOverrideStartingValue"))
	FFixedPoint StartingValueOverride = FFixedPoint::Zero;

	/** If true, override the catalog's default cap with CapOverride. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy")
	bool bOverrideCap = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Economy",
		meta = (EditCondition = "bOverrideCap"))
	FFixedPoint CapOverride = FFixedPoint::Zero;
};

// =====================================================================
// FSeinResourceCost
// =====================================================================

/**
 * Unified cost type used by both abilities and production. Amounts are always
 * authored as positive quantities; the resource's CostDirection determines
 * whether the amount is subtracted from the balance or added toward cap.
 *
 * Validation (CanAfford), application (Deduct), and refund (Refund) route
 * through USeinResourceBPFL; see that library for authoritative call sites.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinResourceCost
{
	GENERATED_BODY()

	/** Positive amounts keyed by resource tag. Empty = free. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Economy",
		meta = (Categories = "SeinARTS.Resource"))
	TMap<FGameplayTag, FFixedPoint> Amounts;

	FORCEINLINE bool IsEmpty() const { return Amounts.Num() == 0; }
	FORCEINLINE int32 Num() const { return Amounts.Num(); }
};

FORCEINLINE uint32 GetTypeHash(const FSeinResourceCost& Cost)
{
	uint32 Hash = 0;
	for (const auto& Pair : Cost.Amounts)
	{
		Hash = HashCombine(Hash, GetTypeHash(Pair.Key));
		Hash = HashCombine(Hash, GetTypeHash(Pair.Value));
	}
	return Hash;
}
