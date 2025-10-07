#pragma once

#include "CoreMinimal.h"
#include "Structs/SAFResources.h"
#include "SAFProductionRecipe.generated.h"

class USAFAsset;

/**
 * FSAFProductionRecipe
 * 
 * A simple struct that contains a reference to a SAFDataAsset (the asset this
 * recipe builds) and a toggle-able bEnabled flag to set if this recipe is allowed 
 * to be built.
 */
USTRUCT(BlueprintType)
struct FSAFProductionRecipe {
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<USAFAsset> Asset;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bEnabled = false;
};
