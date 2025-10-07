#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SAFAttributesRow.generated.h"

// Row driving default values for USAFAttributes.
// Designers will edit these in a DataTable asset.
USTRUCT(BlueprintType)
struct FSAFAttributesRow : public FTableRowBase {
	GENERATED_BODY()

	// Armour
	// ==================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Armour") 		float FrontArmour = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Armour") 		float SideArmour  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Armour") 		float RearArmour  = 0.f;

	// Defense
	// ==================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float MaxHealth  = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float MaxShields = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float MaxMorale  = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float Health     = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float Shields    = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float Morale     = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float Durability = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense") 		float Defense    = 0.f;

	// Combat
	// ==================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") 		float Accuracy       = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") 		float Evasion        = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") 		float WeaponHandling = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") 		float WeaponSkill    = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") 		float CritChance     = 0.05f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") 		float CritMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Combat") 		float Luck           = 0.f;

	// Movement
	// ==================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") 	float MoveSpeed         = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") 	float SprintModifier    = 1.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") 	float Speed             = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") 	float Agility           = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement") 	float FormationSpacing  = 50.f;

	// Production
	// ==================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float BuildTime  = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float BuildSpeed = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost1  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost2  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost3  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost4  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost5  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost6  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost7  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost8  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost9  = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost10 = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost11 = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Production") float Cost12 = 0.f;
	
	// Ranges
	// ==================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ranges") 		float FieldOfView   = 900.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ranges") 		float DefenseRange  = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ranges") 		float ChaseRange    = 1200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ranges") 		float AbilityRadius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ranges") 		float EffectRadius  = 0.f;
};
