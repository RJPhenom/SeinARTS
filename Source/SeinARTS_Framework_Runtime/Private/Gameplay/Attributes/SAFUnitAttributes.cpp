#include "Gameplay/Attributes/SAFUnitAttributes.h"
#include "GameplayEffectExtension.h"
#include "Structs/SAFAttributesRow.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"

USAFUnitAttributes::USAFUnitAttributes() {
	// Armour
	InitFrontArmour(0.f);
	InitSideArmour(0.f);
	InitRearArmour(0.f);

	// Defense
	InitMaxHealth(100.f);
	InitMaxShields(100.f);
	InitMaxMorale(100.f);
	InitHealth(100.f);
	InitShields(100.f);
	InitMorale(100.f);
	InitDurability(0.f);
	InitDefense(0.f);

	// Combat
	InitAccuracy(1.f);
	InitEvasion(0.f);
	InitWeaponHandling(0.f);
	InitWeaponSkill(0.f);
	InitCritChance(0.05f);
	InitCritMultiplier(1.f);
	InitLuck(0.f);

	// Movement
	InitMoveSpeed(600.f);
	InitSprintModifier(1.3f);
	InitSpeed(0.f);
	InitAgility(0.f);
	InitFormationSpacing(50.f);

	// Ranges
	InitFieldOfView(900.f);
	InitDefenseRange(600.f);
	InitChaseRange(1200.f);
	InitAbilityRadius(0.f);
	InitEffectRadius(0.f);
}

// Attribute Lifecycle Overrides
// =========================================================================================================================
// Pre change override helps keep % when max changes; clamp current to new max.
void USAFUnitAttributes::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) {
  Super::PreAttributeChange(Attribute, NewValue);

  // Health
  if (Attribute == GetMaxHealthAttribute())	{
    NewValue = FMath::Max(0.f, NewValue);
    const float OldMax = GetMaxHealth();
    if (OldMax > 0.f) {
      const float Percent = GetHealth() / OldMax;
      SetHealth(FMath::Clamp(Percent * NewValue, 0.f, NewValue));
    } else {
      SetHealth(FMath::Clamp(GetHealth(), 0.f, NewValue));
    }
  }

  // Shields
  else if (Attribute == GetMaxShieldsAttribute()) {
    NewValue = FMath::Max(0.f, NewValue);
    const float OldMax = GetMaxShields();
    if (OldMax > 0.f)	{
      const float Percent = GetShields() / OldMax;
      SetShields(FMath::Clamp(Percent * NewValue, 0.f, NewValue));
    } else {
      SetShields(FMath::Clamp(GetShields(), 0.f, NewValue));
    }
  }

  // Morale
  else if (Attribute == GetMaxMoraleAttribute()) {
    NewValue = FMath::Max(0.f, NewValue);
    const float OldMax = GetMaxMorale();
    if (OldMax > 0.f)	{
      const float Percent = GetMorale() / OldMax;
      SetMorale(FMath::Clamp(Percent * NewValue, 0.f, NewValue));
    } else {
      SetMorale(FMath::Clamp(GetMorale(), 0.f, NewValue));
    }
  }
}

// Post effect override helps clamp to max attributes
void USAFUnitAttributes::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) {
  Super::PostGameplayEffectExecute(Data);

  if (Data.EvaluatedData.Attribute == GetHealthAttribute())	{
    SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
  }	else if (Data.EvaluatedData.Attribute == GetShieldsAttribute())	{
    SetShields(FMath::Clamp(GetShields(), 0.f, GetMaxShields()));
  }	else if (Data.EvaluatedData.Attribute == GetMoraleAttribute())	{
    SetMorale(FMath::Clamp(GetMorale(), 0.f, GetMaxMorale()));
  }
}

// Replication
// =========================================================================================================================
void USAFUnitAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // Armour
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, FrontArmour,    		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, SideArmour,     		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, RearArmour,     		COND_None, REPNOTIFY_Always);

  // Defense
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Health,         		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, MaxHealth,      		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Shields,        		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, MaxShields,     		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Morale,         		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, MaxMorale,      		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Durability,     		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Defense,        		COND_None, REPNOTIFY_Always);

  // Combat
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Accuracy,       		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Evasion,        		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, WeaponHandling, 		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, WeaponSkill,    		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, CritChance,     		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, CritMultiplier, 		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Luck,           		COND_None, REPNOTIFY_Always);

  // Movement
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, MoveSpeed,      		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, SprintModifier, 		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Speed,          		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, Agility,        		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, FormationSpacing,	COND_None, REPNOTIFY_Always);

  // Ranges
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, FieldOfView,    		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, DefenseRange,   		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, ChaseRange,     		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, AbilityRadius,  		COND_None, REPNOTIFY_Always);
  DOREPLIFETIME_CONDITION_NOTIFY(USAFUnitAttributes, EffectRadius,   		COND_None, REPNOTIFY_Always);
}
