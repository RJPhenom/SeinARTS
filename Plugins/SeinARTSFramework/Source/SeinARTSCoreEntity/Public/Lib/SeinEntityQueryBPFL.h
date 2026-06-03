/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:   SeinEntityQueryBPFL.h
 * @brief:  Spatial / distance entity-query helpers. Iterates the entity pool
 *          with simple geometric predicates (sphere, box, pair distance /
 *          direction) and an optional gameplay-tag filter. Not combat-specific
 *          — designers use these from any sim-side BP (abilities, effects,
 *          AI, scenario scripts) that needs to find entities in space.
 *
 *          These helpers are O(N) over the entity pool — fine for incidental
 *          queries. (There is no general-purpose proximity index to lean on:
 *          the framework's spatial hash is the collision broadphase and is
 *          collision-only.)
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinEntityQueryBPFL.generated.h"

class USeinWorldSubsystem;

UCLASS(meta = (DisplayName = "SeinARTS Entity Query Library"))
class SEINARTSCOREENTITY_API USeinEntityQueryBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** All entities within `Radius` of `Origin`. Optional `FilterTags` keeps only
	 *  entities carrying at least one of the listed tags. Empty filter = all entities. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entities In Range"))
	static TArray<FSeinEntityHandle> SeinGetEntitiesInRange(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags);

	/** Closest entity within `Radius` of `Origin` (or invalid handle if none).
	 *  Optional tag filter as above. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Nearest Entity"))
	static FSeinEntityHandle SeinGetNearestEntity(const UObject* WorldContextObject, FFixedVector Origin, FFixedPoint Radius, FGameplayTagContainer FilterTags);

	/** All entities inside the axis-aligned box defined by Min/Max. Optional tag filter. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Entities In Box"))
	static TArray<FSeinEntityHandle> SeinGetEntitiesInBox(const UObject* WorldContextObject, FFixedVector Min, FFixedVector Max, FGameplayTagContainer FilterTags);

	/** Straight-line distance between two entities' world locations. Zero if either is invalid. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Distance Between"))
	static FFixedPoint SeinGetDistanceBetween(const UObject* WorldContextObject, FSeinEntityHandle EntityA, FSeinEntityHandle EntityB);

	/** Unit-length direction vector from `FromEntity` to `ToEntity`. Zero vector if
	 *  either is invalid or the two share a location. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Direction To"))
	static FFixedVector SeinGetDirectionTo(const UObject* WorldContextObject, FSeinEntityHandle FromEntity, FSeinEntityHandle ToEntity);

private:
	static USeinWorldSubsystem* GetWorldSubsystem(const UObject* WorldContextObject);
};
