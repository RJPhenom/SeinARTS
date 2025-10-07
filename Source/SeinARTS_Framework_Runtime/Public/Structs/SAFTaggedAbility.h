#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h" 
#include "Structs/SAFResources.h"
#include "SAFTaggedAbility.generated.h"

class USAFAbility;

/**
 * FSAFTaggedAbility
 * 
 * A simple struct that acts as a tag->ability map for SeinARTS units to
 * easily connect orders to behaviour.
 */
USTRUCT(BlueprintType)
struct FSAFTaggedAbility {
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") FGameplayTagContainer AbilityTags;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") int32 Level = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") int32 InputID = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") TSubclassOf<USAFAbility> AbilityClass;
};