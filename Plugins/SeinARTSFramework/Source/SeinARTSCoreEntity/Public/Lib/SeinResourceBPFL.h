/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinResourceBPFL.h
 * @brief   Unified Blueprint Function Library for the resource layer. Per
 *          the resource catalog model. Read + mutate surfaces for player balances,
 *          cost validation (CanAfford), cost application (Deduct), refunds,
 *          income grants, and cross-player transfers (gated by match settings
 *          once §18 lands — permissive pre-§18).
 *
 *          Writes are callable from any graph here (rather than routed through
 *          USeinSimMutationBPFL) because economy mutation is first-class
 *          gameplay driven by commands and abilities; the DESIGN calls for a
 *          single unified surface. SEIN_CHECK_SIM() backstops dev-build misuse.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "GameplayTagContainer.h"
#include "Data/SeinResourceTypes.h"
#include "SeinResourceBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Resource Library"))
class SEINARTSCOREENTITY_API USeinResourceBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// Reads
	// ====================================================================================================

	/** Get the current balance of a tagged resource for a player. Returns Zero on invalid player / missing entry. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Resource"))
	static FFixedPoint SeinGetResource(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag ResourceTag);

	/** Returns the player's full resource map. Empty on invalid player. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get All Resources"))
	static TMap<FGameplayTag, FFixedPoint> SeinGetAllResources(const UObject* WorldContextObject, FSeinPlayerID PlayerID);

	/** Returns the cap for a resource (per-player override if set, else catalog default). FFixedPoint::Zero means uncapped. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Resource Cap"))
	static FFixedPoint SeinGetResourceCap(const UObject* WorldContextObject, FSeinPlayerID PlayerID, FGameplayTag ResourceTag);

	/** Returns true if the player can afford the full cost. Uses the resource's CostDirection
	 *  (DeductFromBalance vs AddTowardCap) when consulting each entry. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Can Afford"))
	static bool SeinCanAfford(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Cost);

	/** Split a cost map into AtEnqueue / AtCompletion buckets according to each
	 *  resource's `ProductionDeductionTiming` in the catalog. Ability activation
	 *  consults this only when its explicit cost timing is Production Queue.
	 *
	 *  Resources whose tag is not found in the catalog default to AtEnqueue —
	 *  fail-safe for partially-authored projects. Empty input → both outputs empty. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Split Cost By Catalog"))
	static void SeinSplitCostByCatalog(const UObject* WorldContextObject,
		const FSeinResourceCost& InCost,
		FSeinResourceCost& OutAtEnqueueCost,
		FSeinResourceCost& OutAtCompletionCost);

	// Writes
	// ====================================================================================================

	/** Deducts the full cost if affordable. Returns false (no changes) on invalid player or
	 *  insufficient balance. Uses CostDirection to decide the math per entry. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Deduct"))
	static bool SeinDeduct(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Cost);

	/** Refunds a cost back to the player. No affordability gate — always applied. Honors
	 *  cap: excess clamps per catalog overflow behavior. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Refund"))
	static void SeinRefund(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Cost);

	/** One-shot income grant (ability drop, scavenge loot, tech completion bonus). Equivalent to Refund
	 *  semantically — applies regardless of cap-overflow policy with clamp-at-cap. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Grant Income"))
	static void SeinGrantIncome(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FSeinResourceCost& Amount);

	/** Transfer resources from one player to another. Returns false on invalid
	 *  players or insufficient balance. Always permits the transfer when
	 *  affordable — gating (alliance rules, mid-match transfer locks, etc.)
	 *  is designer-authored at the caller. Wrap this BPFL in a project-side
	 *  BP graph that checks your project's policy before invoking. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Economy",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Transfer"))
	static bool SeinTransfer(const UObject* WorldContextObject, FSeinPlayerID FromPlayer, FSeinPlayerID ToPlayer, const FSeinResourceCost& Amount);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
