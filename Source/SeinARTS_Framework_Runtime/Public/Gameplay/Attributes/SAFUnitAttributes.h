#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Gameplay/Attributes/SAFAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "Utils/SAFAttributeMacros.h"
#include "SAFUnitAttributes.generated.h"

struct FSAFAttributesRow;
struct FGameplayEffectModCallbackData;

/**
 * USAFUnitAttributes
 * 
 * Fixed, unit-centric gameplay attributes (armour/defense/combat/movement/ranges).
 * Values are initialized from USAFUnitAsset on spawn.
 */
UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API USAFUnitAttributes : public USAFAttributeSet {

	GENERATED_BODY()

public:

	USAFUnitAttributes();

	// Armour
	// ===================================================================================================
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Armour", ReplicatedUsing=OnRep_FrontArmour)
	FGameplayAttributeData FrontArmour; SAF_ATTR_ACCESSORS(USAFUnitAttributes, FrontArmour)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Armour", ReplicatedUsing=OnRep_SideArmour)
	FGameplayAttributeData SideArmour; SAF_ATTR_ACCESSORS(USAFUnitAttributes, SideArmour)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Armour", ReplicatedUsing=OnRep_RearArmour)
	FGameplayAttributeData RearArmour; SAF_ATTR_ACCESSORS(USAFUnitAttributes, RearArmour)

	// Defense
	// ===================================================================================================
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Health)
	FGameplayAttributeData Health; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Health)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth; SAF_ATTR_ACCESSORS(USAFUnitAttributes, MaxHealth)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Shields)
	FGameplayAttributeData Shields; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Shields)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_MaxShields)
	FGameplayAttributeData MaxShields; SAF_ATTR_ACCESSORS(USAFUnitAttributes, MaxShields)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Morale)
	FGameplayAttributeData Morale; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Morale)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_MaxMorale)
	FGameplayAttributeData MaxMorale; SAF_ATTR_ACCESSORS(USAFUnitAttributes, MaxMorale)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Durability)
	FGameplayAttributeData Durability; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Durability)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Defense", ReplicatedUsing=OnRep_Defense)
	FGameplayAttributeData Defense; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Defense)

	// Combat
	// ===================================================================================================
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_Accuracy)
	FGameplayAttributeData Accuracy; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Accuracy)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_Evasion)
	FGameplayAttributeData Evasion; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Evasion)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_WeaponHandling)
	FGameplayAttributeData WeaponHandling; SAF_ATTR_ACCESSORS(USAFUnitAttributes, WeaponHandling)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_WeaponSkill)
	FGameplayAttributeData WeaponSkill; SAF_ATTR_ACCESSORS(USAFUnitAttributes, WeaponSkill)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_CritChance)
	FGameplayAttributeData CritChance; SAF_ATTR_ACCESSORS(USAFUnitAttributes, CritChance)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_CritMultiplier)
	FGameplayAttributeData CritMultiplier; SAF_ATTR_ACCESSORS(USAFUnitAttributes, CritMultiplier)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Combat", ReplicatedUsing=OnRep_Luck)
	FGameplayAttributeData Luck; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Luck)

	// Movement
	// ===================================================================================================
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_MoveSpeed)
	FGameplayAttributeData MoveSpeed; SAF_ATTR_ACCESSORS(USAFUnitAttributes, MoveSpeed)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_SprintModifier)
	FGameplayAttributeData SprintModifier; SAF_ATTR_ACCESSORS(USAFUnitAttributes, SprintModifier)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_Speed)
	FGameplayAttributeData Speed; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Speed)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_Agility)
	FGameplayAttributeData Agility; SAF_ATTR_ACCESSORS(USAFUnitAttributes, Agility)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Movement", ReplicatedUsing=OnRep_FormationSpacing)
	FGameplayAttributeData FormationSpacing; SAF_ATTR_ACCESSORS(USAFUnitAttributes, FormationSpacing)

	// Ranges
	// ===================================================================================================
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_FieldOfView)
	FGameplayAttributeData FieldOfView; SAF_ATTR_ACCESSORS(USAFUnitAttributes, FieldOfView)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_DefenseRange)
	FGameplayAttributeData DefenseRange; SAF_ATTR_ACCESSORS(USAFUnitAttributes, DefenseRange)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_ChaseRange)
	FGameplayAttributeData ChaseRange; SAF_ATTR_ACCESSORS(USAFUnitAttributes, ChaseRange)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_AbilityRadius)
	FGameplayAttributeData AbilityRadius; SAF_ATTR_ACCESSORS(USAFUnitAttributes, AbilityRadius)
	UPROPERTY(BlueprintReadOnly, Category="SeinARTS|Ranges", ReplicatedUsing=OnRep_EffectRadius)
	FGameplayAttributeData EffectRadius; SAF_ATTR_ACCESSORS(USAFUnitAttributes, EffectRadius)

	// Replication
	// ====================================================================================================================================================
	UFUNCTION() void OnRep_FrontArmour      (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, FrontArmour,      Old); }
	UFUNCTION() void OnRep_SideArmour       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, SideArmour,       Old); }
	UFUNCTION() void OnRep_RearArmour       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, RearArmour,       Old); }

	UFUNCTION() void OnRep_Health           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Health,           Old); }
	UFUNCTION() void OnRep_MaxHealth        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, MaxHealth,        Old); }
	UFUNCTION() void OnRep_Shields          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Shields,          Old); }
	UFUNCTION() void OnRep_MaxShields       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, MaxShields,       Old); }
	UFUNCTION() void OnRep_Morale           (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Morale,           Old); }
	UFUNCTION() void OnRep_MaxMorale        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, MaxMorale,        Old); }
	UFUNCTION() void OnRep_Durability       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Durability,       Old); }
	UFUNCTION() void OnRep_Defense          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Defense,          Old); }

	UFUNCTION() void OnRep_Accuracy         (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Accuracy,         Old); }
	UFUNCTION() void OnRep_Evasion          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Evasion,          Old); }
	UFUNCTION() void OnRep_WeaponHandling   (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, WeaponHandling,   Old); }
	UFUNCTION() void OnRep_WeaponSkill      (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, WeaponSkill,      Old); }
	UFUNCTION() void OnRep_CritChance       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, CritChance,       Old); }
	UFUNCTION() void OnRep_CritMultiplier   (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, CritMultiplier,   Old); }
	UFUNCTION() void OnRep_Luck             (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Luck,             Old); }

	UFUNCTION() void OnRep_MoveSpeed        (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, MoveSpeed,        Old); }
	UFUNCTION() void OnRep_SprintModifier   (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, SprintModifier,   Old); }
	UFUNCTION() void OnRep_Speed            (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Speed,            Old); }
	UFUNCTION() void OnRep_Agility          (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, Agility,          Old); }
	UFUNCTION() void OnRep_FormationSpacing (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, FormationSpacing, Old); }


	UFUNCTION() void OnRep_FieldOfView      (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, FieldOfView,      Old); }
	UFUNCTION() void OnRep_DefenseRange     (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, DefenseRange,     Old); }
	UFUNCTION() void OnRep_ChaseRange       (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, ChaseRange,       Old); }
	UFUNCTION() void OnRep_AbilityRadius    (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, AbilityRadius,    Old); }
	UFUNCTION() void OnRep_EffectRadius     (const FGameplayAttributeData& Old) { GAMEPLAYATTRIBUTE_REPNOTIFY(USAFUnitAttributes, EffectRadius,     Old); }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// GAS Overrides
	// =============================================================================================
	/** Keeps current within new max and maintains ratios when Max* changes. */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** Clamp/route side effects after GE execution (e.g., Health <= MaxHealth). */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

};

