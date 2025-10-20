#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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

	/** The tags associated with this ability */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") FGameplayTagContainer AbilityTags;

	/** The level of the ability */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") int32 Level = 1;

	/** The input ID for the ability */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") int32 InputID = 0;

	/** The class of the ability */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Abilities") TSubclassOf<USAFAbility> AbilityClass;
};