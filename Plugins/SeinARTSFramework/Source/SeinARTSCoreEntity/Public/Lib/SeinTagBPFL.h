/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinTagBPFL.h
 * @author       RJ Macklem
 * @created      27 Mar 2026
 * @latest       14 Aug 2026
 * @brief        Exposes deterministic entity-tag queries and mutations to Blueprint.
 *
 *               Mutations route through USeinWorldSubsystem so refcounts and
 *               the global entity-tag index remain coherent.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "SeinTagBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Tag Library", SeinDeterministic))
class SEINARTSCOREENTITY_API USeinTagBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// ─── Queries (read CombinedTags — refcount > 0 projection) ───

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Has Tag"))
	static bool SeinHasTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Has Any Tag"))
	static bool SeinHasAnyTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Has All Tags"))
	static bool SeinHasAllTags(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer Tags);

	// ─── BaseTags mutations (persistent authoring surface) ───
	//
	// BaseTags is runtime-mutable. Adding to BaseTags both
	// records the tag on the BaseTags authoring container AND grants a refcount
	// so the tag enters CombinedTags / EntityTagIndex.

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Add Base Tag"))
	static bool SeinAddBaseTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Remove Base Tag"))
	static bool SeinRemoveBaseTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Replace Base Tags"))
	static void SeinReplaceBaseTags(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTagContainer NewBaseTags);

	// ─── Transient grants (refcount-only, no BaseTags mutation) ───
	//
	// Use these for ability-, effect-, or component-granted tags that should
	// not persist on the BaseTags authoring container.

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Grant Tag"))
	static void SeinGrantTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Tags", meta = (WorldContext = "WorldContextObject", DisplayName = "Ungrant Tag"))
	static void SeinUngrantTag(const UObject* WorldContextObject, FSeinEntityHandle EntityHandle, FGameplayTag Tag);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
