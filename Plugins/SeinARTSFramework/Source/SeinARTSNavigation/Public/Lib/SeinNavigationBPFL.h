/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationBPFL.h
 * @brief   Blueprint API for navigation queries. Routes through the active
 *          USeinNavigation instance; impl-agnostic.
 */

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "SeinPathTypes.h"
#include "SeinNavigationBPFL.generated.h"

UCLASS(meta = (DisplayName = "SeinARTS Navigation Library"))
class SEINARTSNAVIGATION_API USeinNavigationBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/** Request a path from Start to End for an entity with BlockedTerrainTags.
	 *  Check `bIsValid` on the result before consuming waypoints. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Find Path"))
	static FSeinPath SeinFindPath(
		const UObject* WorldContextObject,
		FFixedVector Start,
		FFixedVector End,
		FSeinEntityHandle Requester,
		FGameplayTagContainer BlockedTerrainTags);

	/** Fast reachability query via the active navigation implementation. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Navigation",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Location Reachable"))
	static bool SeinIsLocationReachable(
		const UObject* WorldContextObject,
		FFixedVector From,
		FFixedVector To,
		FGameplayTagContainer AgentTags);

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Is Path Valid"))
	static bool SeinIsPathValid(const FSeinPath& Path) { return Path.bIsValid; }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Waypoints"))
	static TArray<FFixedVector> SeinGetPathWaypoints(const FSeinPath& Path) { return Path.Waypoints; }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Length"))
	static int32 SeinGetPathLength(const FSeinPath& Path) { return Path.Waypoints.Num(); }

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (DisplayName = "Get Path Cost"))
	static FFixedPoint SeinGetPathCost(const FSeinPath& Path) { return Path.TotalCost; }
};
