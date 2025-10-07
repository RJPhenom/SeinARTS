#pragma once

#include "CoreMinimal.h"
#include "Structs/SAFTaggedAbility.h"
#include "Structs/SAFProductionQueueItem.h"
#include "Structs/SAFProductionRecipe.h"
#include "Structs/SAFResources.h"
#include "Enums/SAFArmourTypes.h"
#include "Enums/SAFStances.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Gameplay/Abilities/SAFAbility.h"
#include "SAFUnitData.generated.h"

class AActor;
class APlayerState;
class UTexture2D;
class USAFAbility;
class UGameplayEffect;
class UAttributeSet;

/**
 * SAFUnitData
 * 
 * Data Asset definition: basic, shared traits of units within the SeinARTS Framework.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Unit Data Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFUnitData : public UPrimaryDataAsset {
	
	GENERATED_BODY()

public:

	static const FPrimaryAssetType PrimaryAssetType;
	virtual FPrimaryAssetId GetPrimaryAssetId() const override {
		return FPrimaryAssetId(PrimaryAssetType, GetFName());
	}

	// ===========================================================================
	//                                  Info
	// ===========================================================================

	/** The display name of the unit, e.g. "Rifleman". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Info")
	FText DisplayName;

	/** Tooltip text for this unit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Info", meta=(MultiLine="true"))
	FText Tooltip;

	/** The unit icon (for UI purposes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Info")
	TSoftObjectPtr<UTexture2D> Icon;

	/** The unit protrait (a large image of the unit for alternate UI purposes). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Info")
	TSoftObjectPtr<UTexture2D> Portait;

	// ===========================================================================
	//                                 Logic
	// ===========================================================================

	/** What class should this unit spawn in as? The default SAFUnit class is designed to
	 * be an abstract base for unit classes and designs. The SeinARTS Framework also supports
	 * custom base unit classes (since it is driven by the SAFUnitInterface) with some extra
	 * work. Actual concrete unit classes are meant to be designed as needed. See the provided
	 * SAFSquad or SAFVehicle as examples. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Logic")
	TSoftClassPtr<AActor> UnitClass;

	/** Controls if this unit can be selected alongside other units, or only single-selected. Set
	 * false when a unit is a building or otherwise should not be selected with a group. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Logic")
	bool bMultiSelectable = true;

	// ===========================================================================
	//                               Production
	// ===========================================================================

	/** Can this unit ever produce another unit? Use this to enable the spawning of an
	 * SAFProductionComponent on initialization. The SAFProductionComponent is what is used by
	 * default to handle production (either unit queues, or builder units building structures). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Production")
	bool bCanEverProduce = false;

	/** Catalogue of production recipes this unit can produce by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Production")
	TArray<FSAFProductionRecipe> ProductionRecipes;
	
	// // Returns an FSAFResources bundle for the default cost profile of the associated unit, using
	// // the associated data table row if set, a zero-bundle if not.
	// UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	// FSAFResources GetDefaultCosts(ESAFResourceRoundingPolicy Policy = ESAFResourceRoundingPolicy::Ceil) const;
	
	// // Returns an FSAFResources bundle for the runtime (GAS-modified) cost profile of the associated
	// // unit, falling back to the associated data table row if unable to resolve attributes.
	// UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	// FSAFResources GetRuntimeCosts(const APlayerState* Playerstate, ESAFResourceRoundingPolicy Policy = ESAFResourceRoundingPolicy::Ceil) const;

	// ===========================================================================
	//                           	      GAS
	// ===========================================================================

	/** Defines which row of the attributes table this data asset should reference. Designers will need
	 * to setup a data table for their unit attributes, rather than manage attributes one-by-one via
	 * data assets (this is to improve balance fixes workflow). Default values are provided if the row
	 * reference is unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GAS")
	FDataTableRowHandle AttributeRow;

	/** Contains a list of the abilites this unit can do. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GAS")
	TArray<FSAFTaggedAbility> Abilities;
	
	/** Contains a list of startup effects for this unit. */
	UPROPERTY(EditAnywhere, Category="GAS") 
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Default gameplay tags this unit should start with. The SeinARTS Framework uses gameplay tags
	 * to manage state, to resolve orders, and to mark units with proficiencies for gameplay purposes.
	 *
	 * Note: You do NOT need to add SeinARTS.Init tags for initialization on SAFUnits, these tags are
	 * used as init keys during the InitGameplayEffect initialization only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GAS")
	FGameplayTagContainer DefaultTags;

	/** Optional extra AttributeSet classes to attach to this unit’s ASC on spawn.
	 * (SAFUnits always have SAFAttributes, use this for other custom attribute sets) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GAS")
	TArray<TSubclassOf<UAttributeSet>> DesignerAttributeSets;

};
