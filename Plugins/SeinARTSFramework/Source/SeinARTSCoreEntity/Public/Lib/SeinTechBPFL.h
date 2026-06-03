/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTechBPFL.h
 * @brief   Tech availability + query BPFL. Tech is unified with effects per
 *          DESIGN §10 — a tech is just a `USeinEffect` subclass. This library
 *          sits on top of that pipeline with availability + researched-state
 *          helpers shaped for UI binding (matches `FSeinProductionAvailability`
 *          and `FSeinAbilityAvailability`).
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinPlayerID.h"
#include "Effects/SeinEffect.h"
#include "SeinTechBPFL.generated.h"

class USeinWorldSubsystem;

/**
 * Aggregate availability snapshot for a single tech (USeinEffect subclass) on a
 * single player. Matches the shape of FSeinAbilityAvailability / FSeinProductionAvailability.
 */
USTRUCT(BlueprintType, meta = (SeinDeterministic))
struct SEINARTSCOREENTITY_API FSeinTechAvailability
{
	GENERATED_BODY()

	/** Tech effect class queried (mirrors input for BP convenience). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	TSubclassOf<USeinEffect> TechClass;

	/** EffectTag pulled from the class CDO (mirrors for BP convenience). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	FGameplayTag TechTag;

	/** Tech is already present on the player (EffectTag refcount > 0). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	bool bAlreadyResearched = false;

	/** All PrerequisiteTags (authored on the research producible) are present. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	bool bPrerequisitesMet = false;

	/** At least one ForbiddenPrerequisiteTag is present. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	bool bForbiddenPresent = false;

	/** Result of the effect's `CanResearch` BP event. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|Tech")
	bool bCustomCheckPassed = false;
};

UCLASS(meta = (DisplayName = "SeinARTS Tech Library"))
class SEINARTSCOREENTITY_API USeinTechBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Build a FSeinTechAvailability snapshot for a tech class on a player.
	 *  Pass `PrerequisiteTags` from the owning research producible if you have them;
	 *  empty container = no prerequisite gate. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tech",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Tech Availability"))
	static FSeinTechAvailability SeinGetTechAvailability(
		const UObject* WorldContextObject,
		FSeinPlayerID PlayerID,
		TSubclassOf<USeinEffect> TechClass,
		const FGameplayTagContainer& PrerequisiteTags);

	/** True if the player carries the tech's EffectTag. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tech",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Tech Researched"))
	static bool SeinIsTechResearched(const UObject* WorldContextObject, FSeinPlayerID PlayerID, TSubclassOf<USeinEffect> TechClass);

	/** True if every tag in `Tags` is present on the player's refcounted tag set. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tech",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Player Has All Tags"))
	static bool SeinPlayerHasAllTags(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FGameplayTagContainer& Tags);

	/** True if at least one tag in `Tags` is present on the player. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tech",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Player Has Any Tag"))
	static bool SeinPlayerHasAnyTag(const UObject* WorldContextObject, FSeinPlayerID PlayerID, const FGameplayTagContainer& Tags);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
