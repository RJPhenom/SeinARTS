#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Structs/SAFTechnologyBundle.h"
#include "Structs/SAFResourceBundle.h"
#include "SAFPlayerTechnologyComponent.generated.h"

class USAFTechnologyAsset;
class USAFUnitAsset;
class USAFAsset;
class UGameplayEffect;

// Event Delegate
// ==============================================================================================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTechnologyTagChanged, FGameplayTag, ChangedTag, int32, NewCount);

/**
 * USAFPlayerTechnologyComponent
 * 
 * Specialized AbilitySystemComponent for managing player technology research state.
 * Tracks technology unlock tags, provides technology modifications to units,
 * and handles cost/time calculations with technology bonuses.
 */
UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable, 
meta=(BlueprintSpawnableComponent, DisplayName="SeinARTS Player Technology Component"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFPlayerTechnologyComponent : public UAbilitySystemComponent {

	GENERATED_BODY()

public:

	// Events Binding
	// =================================================================================
	UPROPERTY(BlueprintAssignable, Category="SeinARTS|Technology")
	FOnTechnologyTagChanged OnTechnologyTagChanged;

    // Event Handler
    // =================================================================================
	/** Handles when a technology tag changes on the player ASC. */
	UFUNCTION() void HandleTechnologyTagChanged(const FGameplayTag Tag, int32 NewCount);

	USAFPlayerTechnologyComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Technology State Query API
	// =======================================================================================================
	/** Returns true if the specified technology has been researched (unlock tag is present). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Technology")
	bool HasTechnology(FGameplayTag TechnologyTag) const;

	/** Returns true if all prerequisite technologies for the given tech asset are researched. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Technology")  
	bool CanResearchTechnology(const USAFTechnologyAsset* TechnologyAsset) const;

	/** Gets all currently researched technology tags. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Technology")
	void GetResearchedTechnologies(FGameplayTagContainer& OutTechnologies) const;

	// Technology Modification API
	// =======================================================================================================
	/** Calculates effective resource costs for the given asset, accounting for technology modifications. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	FSAFResourceBundle ResolveEffectiveResourceCosts(const USAFAsset* Asset) const;

	/** Calculates effective build time for the given asset, accounting for technology modifications. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	float ResolveEffectiveBuildTime(const USAFAsset* Asset) const;

	/** Creates a technology bundle containing all applicable modifications for the given unit asset. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology")
	FSAFTechnologyBundle BuildTechnologyBundleForUnit(const USAFUnitAsset* UnitAsset) const;

	// Technology Research API
	// =======================================================================================================
	/** Research the specified technology (server only). Returns true if research was started successfully. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology", CallInEditor)
	bool ResearchTechnology(const USAFTechnologyAsset* TechnologyAsset);

	/** Instantly complete research of the specified technology (cheat/debug function). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Technology", CallInEditor, meta=(DevelopmentOnly))
	void InstantResearchTechnology(const USAFTechnologyAsset* TechnologyAsset);

	// C++ Calculations
	// =======================================================================================================
	/** Calculates cost multiplier based on researched technologies affecting the given asset types. */
	float CalculateCostMultiplier(const FGameplayTagContainer& AssetTypeTags) const;

	/** Calculates time multiplier based on researched technologies affecting the given asset types. */
	float CalculateTimeMultiplier(const FGameplayTagContainer& AssetTypeTags) const;

protected:

	// Delegate handle for technology tag change events
	FDelegateHandle TechnologyTagChangedHandle;

	// Internal Logic
	// =========================================================================================================================================
	/** Initializes the player's ability system component for technology tracking. */
	void InitializePlayerAbilitySystem();

	/** Applies the effects of a researched technology to the player's state. */
	void ApplyTechnologyEffects(const USAFTechnologyAsset* TechnologyAsset);

	/** Builds gameplay effect spec handles for technology bundle creation. */
	void BuildGameplayEffectSpecs(const TArray<TSubclassOf<UGameplayEffect>>& EffectClasses, TArray<FGameplayEffectSpecHandle>& OutSpecs) const;

	/** Filters technology assets based on unit scope tags. */
	bool DoesTechnologyApplyToUnit(const USAFTechnologyAsset* TechnologyAsset, const USAFUnitAsset* UnitAsset) const;

	/** Notifies all production components about unlocked recipes. */
	void NotifyProductionRecipeUnlocks(const USAFTechnologyAsset* TechnologyAsset);

	/** Notifies all existing units to refresh their technology modifications. */
	void NotifyTechnologyUnitsRefresh(const USAFTechnologyAsset* TechnologyAsset);

	/** Finds a technology asset by its unlock tag using asset registry (optimization opportunity). */
	USAFTechnologyAsset* FindTechnologyAssetByUnlockTag(const FGameplayTag& UnlockTag) const;

	/** Gets all researched technology assets by resolving unlock tags to assets. */
	TArray<USAFTechnologyAsset*> GetAllResearchedTechnologyAssets() const;

	// Replication
	// =========================================================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

};