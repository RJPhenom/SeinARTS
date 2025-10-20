#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemInterface.h"
#include "Structs/SAFTechnologyBundle.h"
#include "SAFTechnologyComponent.generated.h"

class UAbilitySystemComponent;
class USAFUnitAsset;
class USAFPlayerTechnologyComponent;

/**
 * USAFTechnologyComponent
 * 
 * Component attached to ASAFUnit that receives and applies technology modifications.
 * Handles the application of technology bundles, refreshing when technologies change,
 * and managing technology-spawned sub-actors and equipment.
 */
UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable, 
meta=(BlueprintSpawnableComponent, DisplayName="SeinARTS Technology Component"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFTechnologyComponent : public UActorComponent {

	GENERATED_BODY()

public:

	USAFTechnologyComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Technology Application API
	// =======================================================================================================
	/** Applies the initial technology bundle when the unit spawns. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	void ApplyTechnologyBundleAtSpawn(const USAFUnitAsset* UnitAsset);

	/** Refreshes technology effects when a technology change occurs. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	void RefreshTechnologyEffects(FGameplayTag ChangedTechnologyTag);

	/** Refreshes all technology modifications by rebuilding and reapplying the complete bundle. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	void RefreshTechnologyModifications();

	/** Manually applies a technology bundle (for testing or special cases). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	void ApplyTechnologyBundle(const FSAFTechnologyBundle& TechnologyBundle);

	/** Clears all currently applied technology effects, abilities, attribute sets, and tags. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	void ClearAllTechnologyEffects();

protected:

	// Technology System Constants
	// =======================================================================================================
	/** Gameplay tag used to identify technology-granted effects, abilities, and tags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SeinARTS")
	FGameplayTag TechnologySourceTag = FGameplayTag::RequestGameplayTag("Technology.Source");



	// Internal Application Logic
	// =======================================================================================================
	/** Gets the unit's ability system component from the owner. */
	UAbilitySystemComponent* GetUnitAbilitySystemComponent() const;

	/** Gets the unit asset from the owner. */
	const USAFUnitAsset* GetUnitAsset() const;

	/** Applies gameplay effects from a technology bundle to the unit's ASC. */
	void ApplyTechnologyEffects(const TArray<FGameplayEffectSpecHandle>& EffectSpecs);

	/** Applies gameplay abilities from a technology bundle to the unit's ASC. */
	void ApplyTechnologyAbilities(const TArray<FGameplayAbilitySpec>& AbilitySpecs);

	/** Applies attribute sets from a technology bundle to the unit's ASC. */
	void ApplyTechnologyAttributeSets(const TArray<TSubclassOf<UAttributeSet>>& AttributeSetClasses);

	/** Applies gameplay tags from a technology bundle to the unit's ASC. */
	void ApplyTechnologyTags(const FGameplayTagContainer& Tags);

	/** Clears all technology-sourced modifications from the unit's ASC using GAS queries. */
	void ClearAllTechnologyModifications();



	// Player Technology Integration
	// =======================================================================================================
	/** Gets the player technology component for the unit's owner. */
	USAFPlayerTechnologyComponent* GetPlayerTechnologyComponent() const;

	/** Handles when the player's technology state changes. */
	UFUNCTION()
	void OnPlayerTechnologyChanged(FGameplayTag ChangedTag, int32 NewCount);

	/** Rebuilds and reapplies the complete technology bundle for this unit. */
	void RebuildAndApplyTechnologyBundle();



};