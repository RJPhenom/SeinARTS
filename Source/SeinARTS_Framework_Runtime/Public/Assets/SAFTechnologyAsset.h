#pragma once

#include "CoreMinimal.h"
#include "Assets/SAFAsset.h"
#include "GameplayTagContainer.h"
#include "Structs/SAFResourceBundle.h"
#include "Structs/SAFProductionRecipe.h"
#include "Structs/SAFTaggedAbility.h"
#include "SAFTechnologyAsset.generated.h"

class UGameplayEffect;
class UAttributeSet;
class USAFUnitAsset;

/**
 * USAFTechnologyAsset
 * 
 * Data Asset representing a research technology that can modify units, unlock new capabilities,
 * or provide strategic advantages. Technologies are researched through the production system
 * and apply their effects through the technology component system.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Technology Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFTechnologyAsset : public USAFAsset {

	GENERATED_BODY()

public:

	USAFTechnologyAsset(const FObjectInitializer& ObjectInitializer);

	// Research Requirements
	// ===============================================================================================================================
	/** Technologies that must be researched before this technology becomes available. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Research Requirements")
	FGameplayTagContainer PrerequisiteTechnologies;

	/** Resource cost required to research this technology. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Research Requirements")
	FSAFResourceBundle ResearchCost;

	/** Time in seconds required to complete research of this technology. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Research Requirements", meta=(ClampMin="0.1"))
	float ResearchTime = 30.0f;

	// Target Scope Filtering
	// ===============================================================================================================================
	/** Gameplay tags that define which units this technology affects (e.g., Unit.Archetype.Tank, Unit.Faction.SAS). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Target Filtering")
	FGameplayTagContainer UnitScopeTags;

	/** If true, this technology affects all units globally, ignoring TargetUnitScopes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Target Filtering")
	bool bAppliesToAllUnits = false;

	// Direct GAS Modifications
	// ===============================================================================================================================
	/** Gameplay effects that will be applied to matching units when this technology is researched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Ability System")
	TArray<TSubclassOf<UGameplayEffect>> GrantedGameplayEffects;

	/** Gameplay abilities that will be granted to matching units when this technology is researched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Ability System")
	TArray<FSAFTaggedAbility> GrantedGameplayAbilities;

	/** Attribute sets that will be added to matching units when this technology is researched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Ability System")
	TArray<TSubclassOf<UAttributeSet>> GrantedAttributeSets;

	/** Gameplay tags that will be granted to matching units when this technology is researched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Ability System")
	FGameplayTagContainer GrantedGameplayTags;

	// Unlocking System Integration
	// ===============================================================================================================================
	/** Gameplay tag that will be added to the player's technology component when this tech is researched. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Technology System")
	FGameplayTag TechnologyUnlockTag;

	/** Production recipes that become available after researching this technology. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Technology System")
	TArray<FSAFProductionRecipe> UnlockedProductionRecipes;

protected:

	virtual void PostLoad() override;

	/** Validates that all required tags are properly configured. */
	void ValidateTechnologyConfiguration();

};