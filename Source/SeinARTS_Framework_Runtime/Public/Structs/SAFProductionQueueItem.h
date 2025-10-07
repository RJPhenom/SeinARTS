#pragma once

#include "CoreMinimal.h"
#include "Structs/SAFResources.h"
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<USAFAsset> Asset;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform SpawnTransform = FTransform::Identity;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bRouteToRallyPoint = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FSAFResources CostsBundle = FSAFResources{};
};
