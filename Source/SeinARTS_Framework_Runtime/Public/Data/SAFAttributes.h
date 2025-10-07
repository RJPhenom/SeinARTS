#pragma once

#include "CoreMinimal.h"
#include "Structs/SAFResources.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"
#include "SAFAttributes.generated.h"

struct FGameplayEffectModCallbackData; 

#define SAF_ATTR_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * USAFAttributes
 * 
 * Fixed attributes that power base data for SeinARTS units.
 * Values are initialized from USAFUnitData on spawn.
 */
UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API USAFAttributes : public UAttributeSet {

	GENERATED_BODY()

public:

	/** ===========================================================================
	 * Armour
	 * =========================================================================== */

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Armour", ReplicatedUsing=OnRep_FrontArmour)
	FGameplayAttributeData FrontArmour;              SAF_ATTR_ACCESSORS(USAFAttributes, FrontArmour)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Armour", ReplicatedUsing=OnRep_SideArmour)
	FGameplayAttributeData SideArmour;               SAF_ATTR_ACCESSORS(USAFAttributes, SideArmour)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Armour", ReplicatedUsing=OnRep_RearArmour)
	FGameplayAttributeData RearArmour;               SAF_ATTR_ACCESSORS(USAFAttributes, RearArmour)

	/** ===========================================================================
	 * Defense
	 * =========================================================================== */

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Health)
	FGameplayAttributeData Health;                   SAF_ATTR_ACCESSORS(USAFAttributes, Health)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;                SAF_ATTR_ACCESSORS(USAFAttributes, MaxHealth)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Shields)
	FGameplayAttributeData Shields;                  SAF_ATTR_ACCESSORS(USAFAttributes, Shields)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_MaxShields)
	FGameplayAttributeData MaxShields;               SAF_ATTR_ACCESSORS(USAFAttributes, MaxShields)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Morale)
	FGameplayAttributeData Morale;                   SAF_ATTR_ACCESSORS(USAFAttributes, Morale)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_MaxMorale)
	FGameplayAttributeData MaxMorale;                SAF_ATTR_ACCESSORS(USAFAttributes, MaxMorale)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Durability)
	FGameplayAttributeData Durability;               SAF_ATTR_ACCESSORS(USAFAttributes, Durability)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Defense)
	FGameplayAttributeData Defense;                  SAF_ATTR_ACCESSORS(USAFAttributes, Defense)

	/** ===========================================================================
	 * Combat
	 * =========================================================================== */

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_Accuracy)
	FGameplayAttributeData Accuracy;                 SAF_ATTR_ACCESSORS(USAFAttributes, Accuracy)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_Evasion)
	FGameplayAttributeData Evasion;                  SAF_ATTR_ACCESSORS(USAFAttributes, Evasion)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_WeaponHandling)
	FGameplayAttributeData WeaponHandling;           SAF_ATTR_ACCESSORS(USAFAttributes, WeaponHandling)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_WeaponSkill)
	FGameplayAttributeData WeaponSkill;              SAF_ATTR_ACCESSORS(USAFAttributes, WeaponSkill)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_CritChance)
	FGameplayAttributeData CritChance;               SAF_ATTR_ACCESSORS(USAFAttributes, CritChance)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_CritMultiplier)
	FGameplayAttributeData CritMultiplier;           SAF_ATTR_ACCESSORS(USAFAttributes, CritMultiplier)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_Luck)
	FGameplayAttributeData Luck;                     SAF_ATTR_ACCESSORS(USAFAttributes, Luck)

	/** ===========================================================================
	 * Movement
	 * =========================================================================== */

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_MoveSpeed)
	FGameplayAttributeData MoveSpeed;                SAF_ATTR_ACCESSORS(USAFAttributes, MoveSpeed)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_SprintModifier)
	FGameplayAttributeData SprintModifier;           SAF_ATTR_ACCESSORS(USAFAttributes, SprintModifier)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_Speed)
	FGameplayAttributeData Speed;                    SAF_ATTR_ACCESSORS(USAFAttributes, Speed)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_Agility)
	FGameplayAttributeData Agility;                  SAF_ATTR_ACCESSORS(USAFAttributes, Agility)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_FormationSpacing)
	FGameplayAttributeData FormationSpacing;         SAF_ATTR_ACCESSORS(USAFAttributes, FormationSpacing)

	/** ===========================================================================
	 * Production
	 * =========================================================================== */

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_BuildTime)
	FGameplayAttributeData BuildTime;               SAF_ATTR_ACCESSORS(USAFAttributes, BuildTime)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_BuildSpeed)
	FGameplayAttributeData BuildSpeed;               SAF_ATTR_ACCESSORS(USAFAttributes, BuildSpeed)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost1)
	FGameplayAttributeData Cost1;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost1)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost2)
	FGameplayAttributeData Cost2;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost2)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost3)
	FGameplayAttributeData Cost3;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost3)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost4)
	FGameplayAttributeData Cost4;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost4)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost5)
	FGameplayAttributeData Cost5;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost5)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost6)
	FGameplayAttributeData Cost6;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost6)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost7)
	FGameplayAttributeData Cost7;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost7)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost8)
	FGameplayAttributeData Cost8;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost8)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost9)
	FGameplayAttributeData Cost9;                    SAF_ATTR_ACCESSORS(USAFAttributes, Cost9)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost10)
	FGameplayAttributeData Cost10;                   SAF_ATTR_ACCESSORS(USAFAttributes, Cost10)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost11)
	FGameplayAttributeData Cost11;                   SAF_ATTR_ACCESSORS(USAFAttributes, Cost11)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Production", ReplicatedUsing=OnRep_Cost12)
	FGameplayAttributeData Cost12;                   SAF_ATTR_ACCESSORS(USAFAttributes, Cost12)

	/** Returns the cost attributes as a FSAFResources bundle. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	FSAFResources BundleResources(ESAFResourceRoundingPolicy Policy = ESAFResourceRoundingPolicy::Ceil) const;

	/** ===========================================================================
	 * Ranges
	 * =========================================================================== */

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_FieldOfView)
	FGameplayAttributeData FieldOfView;              SAF_ATTR_ACCESSORS(USAFAttributes, FieldOfView)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_DefenseRange)
	FGameplayAttributeData DefenseRange;             SAF_ATTR_ACCESSORS(USAFAttributes, DefenseRange)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_ChaseRange)
	FGameplayAttributeData ChaseRange;               SAF_ATTR_ACCESSORS(USAFAttributes, ChaseRange)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_AbilityRadius)
	FGameplayAttributeData AbilityRadius;            SAF_ATTR_ACCESSORS(USAFAttributes, AbilityRadius)

	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_EffectRadius)
	FGameplayAttributeData EffectRadius;             SAF_ATTR_ACCESSORS(USAFAttributes, EffectRadius)

	/** ===========================================================================
	 * Replication
	 * =========================================================================== */

	UFUNCTION() void OnRep_FrontArmour      (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, FrontArmour,      Old); }
	UFUNCTION() void OnRep_SideArmour       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, SideArmour,       Old); }
	UFUNCTION() void OnRep_RearArmour       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, RearArmour,       Old); }

	UFUNCTION() void OnRep_Health           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Health,           Old); }
	UFUNCTION() void OnRep_MaxHealth        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, MaxHealth,        Old); }
	UFUNCTION() void OnRep_Shields          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Shields,          Old); }
	UFUNCTION() void OnRep_MaxShields       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, MaxShields,       Old); }
	UFUNCTION() void OnRep_Morale           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Morale,           Old); }
	UFUNCTION() void OnRep_MaxMorale        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, MaxMorale,        Old); }
	UFUNCTION() void OnRep_Durability       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Durability,       Old); }
	UFUNCTION() void OnRep_Defense          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Defense,          Old); }

	UFUNCTION() void OnRep_Accuracy         (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Accuracy,         Old); }
	UFUNCTION() void OnRep_Evasion          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Evasion,          Old); }
	UFUNCTION() void OnRep_WeaponHandling   (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, WeaponHandling,   Old); }
	UFUNCTION() void OnRep_WeaponSkill      (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, WeaponSkill,      Old); }
	UFUNCTION() void OnRep_CritChance       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, CritChance,       Old); }
	UFUNCTION() void OnRep_CritMultiplier   (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, CritMultiplier,   Old); }
	UFUNCTION() void OnRep_Luck             (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Luck,             Old); }

	UFUNCTION() void OnRep_MoveSpeed        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, MoveSpeed,        Old); }
	UFUNCTION() void OnRep_SprintModifier   (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, SprintModifier,   Old); }
	UFUNCTION() void OnRep_Speed            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Speed,            Old); }
	UFUNCTION() void OnRep_Agility          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Agility,          Old); }
	UFUNCTION() void OnRep_FormationSpacing (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, FormationSpacing, Old); }

	UFUNCTION() void OnRep_BuildTime        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, BuildTime,        Old); }
	UFUNCTION() void OnRep_BuildSpeed       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, BuildSpeed,       Old); }
	UFUNCTION() void OnRep_Cost1            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost1,            Old); }
	UFUNCTION() void OnRep_Cost2            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost2,            Old); }
	UFUNCTION() void OnRep_Cost3            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost3,            Old); }
	UFUNCTION() void OnRep_Cost4            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost4,            Old); }
	UFUNCTION() void OnRep_Cost5            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost5,            Old); }
	UFUNCTION() void OnRep_Cost6            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost6,            Old); }
	UFUNCTION() void OnRep_Cost7            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost7,            Old); }
	UFUNCTION() void OnRep_Cost8            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost8,            Old); }
	UFUNCTION() void OnRep_Cost9            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost9,            Old); }
	UFUNCTION() void OnRep_Cost10           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost10,           Old); }
	UFUNCTION() void OnRep_Cost11           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost11,           Old); }
	UFUNCTION() void OnRep_Cost12           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, Cost12,           Old); }

	UFUNCTION() void OnRep_FieldOfView      (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, FieldOfView,      Old); }
	UFUNCTION() void OnRep_DefenseRange     (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, DefenseRange,     Old); }
	UFUNCTION() void OnRep_ChaseRange       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, ChaseRange,       Old); }
	UFUNCTION() void OnRep_AbilityRadius    (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, AbilityRadius,    Old); }
	UFUNCTION() void OnRep_EffectRadius     (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFAttributes, EffectRadius,     Old); }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// ===========================================================================
	//                               GAS Overrides
	// ===========================================================================

	// Pre change override helps keep % when max changes; clamp current to new max.
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	// Post effect override helps clamp to max attributes
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

};
