#pragma once

#include "CoreMinimal.h"
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

	/** The asset this recipe produces/researches. Can be USAFUnitAsset, USAFTechnologyAsset, or other SAFAsset types. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Production Recipe") 
	TSoftObjectPtr<USAFAsset> Asset;

	/** Whether this recipe is enabled and can be produced/researched. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Production Recipe") 
	bool bEnabled = false;
	
};
