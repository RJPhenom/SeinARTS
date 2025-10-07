#pragma once

#include "CoreMinimal.h"
#include "Assets/SAFAsset.h"
#include "Engine/DataAsset.h"
#include "Enums/SAFArmourTypes.h"
#include "Enums/SAFStances.h"
#include "GameplayTagContainer.h"
#include "Structs/SAFTaggedAbility.h"
#include "Structs/SAFProductionRecipe.h"
#include "Structs/SAFResources.h"
#include "SAFUnitAsset.generated.h"

class AActor;
class APlayerState;
class UTexture2D;
class USAFAbility;
class UGameplayEffect;
class UAttributeSet;

/**
 * SAFUnitAsset
 * 
 * Data Asset definition: basic, shared traits of units within the SeinARTS Framework.
 * This is the base asset blueprint for building units off of. You can subclass this for your own
 * unit classes, just be sure to also appropriately set (and write) the relevant runtime class
 * that this data should feed into.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Unit Data Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFUnitAsset : public USAFAsset {
	
	GENERATED_BODY()

public:

	USAFUnitAsset(const FObjectInitializer& ObjectInitializer);

	// Production Flags / Recipes
	// ====================================================================================================================
	/* Can this unit ever produce another unit or tech? Use this to enable the spawning of an SAFProductionComponent on 
	initialization. The SAFProductionComponent is what is used by	default to handle production (either unit queues, or 
	builder units building structures). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Production")
	bool bCanEverProduce = false;

	/* Catalogue of production recipes this unit can produce by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Production")
	TArray<FSAFProductionRecipe> ProductionRecipes;

	// GAS Properties
	// ====================================================================================================================	
	/** Contains a list of startup effects for this unit. */
  UPROPERTY(EditAnywhere, Category="GAS") 
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Contains a list of the abilites this unit can do. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GAS")
	TArray<FSAFTaggedAbility> Abilities;

	/** Attribute sets this unit will have. Default SAFUnits *should* always have:
	 * 	SAFUnitAttributeSet
	 * 	SAFProductionAttributeSet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GAS")
	TArray<TSubclassOf<UAttributeSet>> AttributeSets;

  /** Data table rows to easily manage attribute sets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GAS")
	TArray<FDataTableRowHandle> AttributeTableRows;

protected:

	virtual void PostLoad() override;

};
