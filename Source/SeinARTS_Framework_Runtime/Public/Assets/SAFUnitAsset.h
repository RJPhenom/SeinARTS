#pragma once

#include "CoreMinimal.h"
#include "Assets/SAFAsset.h"
#include "Engine/DataAsset.h"
#include "Enums/SAFArmourTypes.h"
#include "Enums/SAFStances.h"
#include "GameplayTagContainer.h"
#include "Structs/SAFTaggedAbility.h"
#include "Structs/SAFProductionRecipe.h"
#include "Assets/SAFPawnAsset.h"
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
	 
	/** The pawn avatar asset that will represent this unit. If unset, this actor will not have pawn representation (appropriate for
	 * static kinds of units like buildings or turrets which do not use navigation or movement) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="!bSquadUnit", EditConditionHides))
	TSoftObjectPtr<USAFPawnAsset> Pawn;

	/** Ordered list of pawns that make up the squad members of this unit. If unset, this unit will self-cull 
	 * (squads must be represented by pawns). By default, index 0 = squad leader. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="bSquadUnit", EditConditionHides))
	TArray<TSoftObjectPtr<USAFPawnAsset>> Pawns;

	/** Overrides the default pawn instance class that this unit will create when it initializes with pawn representation. By default the
	 * SeinARTS Pawn class is used, but this setting allows for custom overrides. (You will still be required to implement the necessary 
	 * pawn interface and logic in your custom class). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Pawn Configuration", 
	meta=(MustImplement="SAFPawnInterface", AllowAbstract="false"))
	TSoftClassPtr<APawn> PawnInstanceClass;

	/** Ordered list of pawn positions in the squad-level formation (different from unit-level formation under the 
	 * SAFFormationManager). Note: if you want your squad to be able to change squad-level formations, you can write to 
	 * the live instance Positions, this is read-only data that will only be used on init calls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pawn Configuration", meta=(EditCondition="bSquadUnit", EditConditionHides))
	TArray<FVector> Positions;

	// Cover Flags / Modifiers
	// ===============================================================================================================================
	/** Whether this unit can use cover mechanics. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cover")
	bool bUsesCover = true;

	/** If this unit has multiple pawns, if they have bUsesCover enabled, this flag sets wether or not they will wrap around corners 
	 * when finding cover positions. If off, pawns will stack in flat rows along the edge of cover. Defaults to on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(EditCondition="bUsesCover && bSquadUnit", EditConditionHides))
	bool bWrapsCover = true;

	/** If this unit has multiple pawns, if they have bUsesCover enabled, this flag adds a tiny amount of scatter to spacing while 
	 * multiple pawns are in cover for a more organic look and feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(EditCondition="bUsesCover && bSquadUnit", EditConditionHides))
	bool bScattersInCover = true;

	/** Modifies how far back the next row will be when stacking multiple pawns in rows behind a cover object. 
	 * Calculated: pawn bounds * extent * modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(EditCondition="bUsesCover && bSquadUnit", EditConditionHides))
	float CoverRowOffsetModifier = 1.f;

	/** Modifies how far back the next row will be when stacking multiple rows in cover behind a cover object. 
	 * Calculated: pawn bounds * extent * modifiers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(EditCondition="bUsesCover && bSquadUnit", EditConditionHides))
	float LateralStaggerModifier = 1.f;

	/** Modifies how far apart the next pawn will be when in cover behind
	 * a cover object. Default is 3. Calculated using:
	 *
	 * SquadCharacter->GetCapsuleComponent()->GetScaledCapsuleRadius() * modifier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cover", meta=(EditCondition="bUsesCover && bSquadUnit", EditConditionHides))
	float CoverSpacingModifier = 2.f;

	// Fallback Spacing
	// ==================================================================================================
	/** Sets the spacing fallback value. When a formation tries to space out multiple units, it will
	 * attempt to check their actor bounds extent (or the bounds extent of their pawn representation). 
	 * If that fails, for whatever reason, this fallback value will be used. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Formation Fallback Spacing")
	float FallbackSpacing = 50.f;

	// GAS Properties
	// ======================================================================================================
	/** Contains a list of startup effects for this unit. */
	UPROPERTY(EditAnywhere, Category="Gameplay Ability System") 
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Contains a list of the abilites this unit can do. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Ability System")
	TArray<FSAFTaggedAbility> Abilities;

	/** Attribute sets this unit will have. Default SAFUnits *should* always have:
	 * 	SAFUnitAttributeSet
	 * 	SAFProductionAttributeSet. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Ability System")
	TArray<TSubclassOf<UAttributeSet>> AttributeSets;

	/** Data table rows to easily manage attribute sets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Gameplay Ability System")
	TArray<FDataTableRowHandle> AttributeTableRows;

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

protected:

	virtual void PostLoad() override;

};
