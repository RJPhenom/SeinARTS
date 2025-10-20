#pragma once

#include "CoreMinimal.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "AttributeSet.h"
#include "SAFTechnologyBundle.generated.h"

/**
 * FSAFTechnologyBundle
 * 
 * Complete package of GAS-based technology modifications that can be applied to a unit.
 * Contains only core GAS elements: effects, abilities, attribute sets, and tags.
 * Advanced features like equipment and composition are handled by specialized GameplayEffects.
 */
USTRUCT(BlueprintType)
struct SEINARTS_FRAMEWORK_RUNTIME_API FSAFTechnologyBundle {
	GENERATED_BODY()

	/** Gameplay effects to apply to the unit's ASC. */
	UPROPERTY(BlueprintReadOnly, Category="Technology Bundle")
	TArray<FGameplayEffectSpecHandle> GameplayEffects;

	/** Gameplay abilities to grant to the unit's ASC. */
	UPROPERTY(BlueprintReadOnly, Category="Technology Bundle")
	TArray<FGameplayAbilitySpec> GameplayAbilities;

	/** Attribute sets to add to the unit's ASC. */
	UPROPERTY(BlueprintReadOnly, Category="Technology Bundle")
	TArray<TSubclassOf<UAttributeSet>> AttributeSets;

	/** Gameplay tags to grant to the unit's ASC. */
	UPROPERTY(BlueprintReadOnly, Category="Technology Bundle")
	FGameplayTagContainer GameplayTags;

	/** Resets all arrays to empty state. */
	void Reset() {
		GameplayEffects.Reset();
		GameplayAbilities.Reset();
		AttributeSets.Reset();
		GameplayTags.Reset();
	}

	/** Returns true if bundle contains no modifications. */
	bool IsEmpty() const {
		return GameplayEffects.Num() == 0 && 
			   GameplayAbilities.Num() == 0 && 
			   AttributeSets.Num() == 0 &&
			   GameplayTags.IsEmpty();
	}
};