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
#include "Assets/Units/SAFPawnAsset.h"
#include "SAFUnitAsset.generated.h"

class AActor;
class APlayerState;
class UTexture2D;
class UAttributeSet;
class UGameplayEffect;
class USAFAbility;

/**
 * SAFUnitAsset
 * 
 * Data Asset definition: basic, shared traits of units within the SeinARTS Framework.
 * This is the base asset blueprint for building units off of. You can subclass this for your own
 * unit classes, just be sure to also appropriately set (and write) the relevant runtime class
 * that this data should feed into.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Unit Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFUnitAsset : public USAFAsset {
	
	GENERATED_BODY()

public:

	USAFUnitAsset(const FObjectInitializer& ObjectInitializer);

	// Pawn(s) Configuration
	// ===============================================================================================================================
	/** Toggles if this unit is represented by a single pawn or multiple pawns in a squad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration")
	bool bSquadUnit = false;
	 
	/** The pawn avatar asset that will represent this unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="!bSquadUnit", EditConditionHides))
	TSoftObjectPtr<USAFPawnAsset> Pawn;

	/** Ordered list of pawns that make up the squad members of this squad unit. By default, index 0 = squad leader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="bSquadUnit", EditConditionHides))
	TArray<TSoftObjectPtr<USAFPawnAsset>> Pawns;

	/** Ordered list of pawn positions in the squad-level formation (different from unit-level formation under the 
	 * SAFFormationManager). Note: if you want your squad to be able to change squad-level formations, you can write to 
	 * the live instance Positions, this is read-only data that will only be used on init calls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="bSquadUnit", EditConditionHides))
	TArray<FVector> PositionsU;

	/** Modifies how far back the next row will be when stacking multiple pawns in rows behind a cover object. 
	 * Calculated: pawn bounds * extent * modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="bSquadUnit", EditConditionHides))
	float CoverRowOffsetModifierU = 1.f;

	/** Modifies how far back the next row will be when stacking multiple rows in cover behind a cover object. 
	 * Calculated: pawn bounds * extent * modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="bSquadUnit", EditConditionHides))
	float LateralStaggerModifierU = 1.f;

	/** Modifies how far apart the next SquadMember will be when in cover behind
	 * a cover object. Default is 3. Calculated using:
	 *
	 * SquadCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius() * modifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="bSquadUnit", EditConditionHides))
	float CoverSpacingModifierU = 2.f;

	// Pawn Cover Flags
	// ===============================================================================================================================
	/** If this unit has multiple pawns, if they have bUsesCover enabled, this flag sets wether or not they will wrap around corners 
	 * when finding cover positions. If off, pawns will stack in flat rows along the edge of cover. Defaults to on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(EditCondition="bSquadUnit", EditConditionHides))
	bool bWrapsCover = true;

	/** If this unit has multiple pawns, if they have bUsesCover enabled, this flag adds a tiny amount of scatter to spacing while 
	 * multiple pawns are in cover for a more organic look and feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(EditCondition="bSquadUnit", EditConditionHides))
	bool bScattersInCover = true;

	// Production Flags / Recipes
	// =================================================================================================================
	/** Can this unit ever produce another unit or tech? Use this to enable the spawning of an SAFProductionComponent on 
	initialization. The SAFProductionComponent is what is used by	default to handle production (either unit queues, or 
	builder units building structures). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Production")
	bool bCanEverProduce = false;

	/** Catalogue of production recipes this unit can produce by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Production", meta=(EditCondition="bCanEverProduce", EditConditionHides))
	TArray<FSAFProductionRecipe> ProductionRecipes;

	// GAS Properties
	// ======================================================================================================
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
