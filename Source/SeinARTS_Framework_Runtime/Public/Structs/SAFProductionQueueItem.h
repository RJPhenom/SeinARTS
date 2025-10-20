#pragma once

#include "CoreMinimal.h"
#include "Structs/SAFResourceBundle.h"
#include "SAFProductionQueueItem.generated.h"

class USAFAsset;

/**
 * FSAFProductionQueueItem
 * 
 * A representation of an item in a production queue. References a SAFDataAsset
 * as the source item, with a spawn transform, rally point routing flag, and a 
 * resource bundle (which represents the resources spent at queue time, for refunds)
 */
USTRUCT(BlueprintType)
struct FSAFProductionQueueItem {
	GENERATED_BODY()

	/** The asset being produced or researched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<USAFAsset> Asset;

	/** The transform to spawn the asset at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform SpawnTransform = FTransform::Identity;

	/** Whether to route the produced unit to a rally point after spawning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRouteToRallyPoint = true;

	/** The resource costs associated with this production item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FSAFResourceBundle CostsBundle = FSAFResourceBundle{};
};
