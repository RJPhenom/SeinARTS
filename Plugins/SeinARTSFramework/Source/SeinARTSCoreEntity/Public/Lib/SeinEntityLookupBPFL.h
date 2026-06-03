/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinEntityLookupBPFL.h
 * @brief   Blueprint access to the global tag → entity index and the
 *          named-entity registry. Backed by USeinWorldSubsystem state
 *          maintained by the refcount-based tag system.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "SeinEntityLookupBPFL.generated.h"

class USeinWorldSubsystem;

/** Blueprint-callable predicate for FindEntities. Designer binds a small BP
 *  function (input: handle, returns: bool). Called once per entity. */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(bool, FSeinEntityFilterPredicate, FSeinEntityHandle, Handle);

UCLASS(meta = (DisplayName = "SeinARTS Entity Lookup Library"))
class SEINARTSCOREENTITY_API USeinEntityLookupBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	// ─── Tag-indexed lookups ───

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Lookup Entities By Tag"))
	static TArray<FSeinEntityHandle> SeinLookupEntitiesByTag(const UObject* WorldContextObject, FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Lookup First Entity By Tag"))
	static FSeinEntityHandle SeinLookupFirstEntityByTag(const UObject* WorldContextObject, FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Count Entities By Tag"))
	static int32 SeinCountEntitiesByTag(const UObject* WorldContextObject, FGameplayTag Tag);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Has Any Entity With Tag"))
	static bool SeinHasAnyEntityWithTag(const UObject* WorldContextObject, FGameplayTag Tag);

	// ─── Named entity registry ───

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Register Named Entity"))
	static void SeinRegisterNamedEntity(const UObject* WorldContextObject, FName Name, FSeinEntityHandle Handle);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Lookup Named Entity"))
	static FSeinEntityHandle SeinLookupNamedEntity(const UObject* WorldContextObject, FName Name);

	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Unregister Named Entity"))
	static void SeinUnregisterNamedEntity(const UObject* WorldContextObject, FName Name);

	// ─── Predicate iteration (slow; prefer tag lookups when expressible as tags) ───

	/** Iterate every live entity and collect those that pass the predicate.
	 *  The predicate is a designer-authored BP function (input: handle, returns: bool).
	 *  **O(total entity count)** — prefer `SeinLookupEntitiesByTag` when the filter can
	 *  be expressed as a gameplay tag. Use this for "find units with build cost > N"
	 *  or similar arbitrary-field predicates. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity|Lookup",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Find Entities (Slow)"))
	static TArray<FSeinEntityHandle> SeinFindEntities(const UObject* WorldContextObject, FSeinEntityFilterPredicate Predicate);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
