#include "Data/SAFAttributes.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

// ===========================================================================
//                        Attribute Lifecycle Overrides
// ===========================================================================

// Pre change override helps keep % when max changes; clamp current to new max.
void USAFAttributes::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) {
	// Health
	if (Attribute == GetMaxHealthAttribute())	{
		NewValue = FMath::Max(0.f, NewValue);
		const float OldMax = GetMaxHealth();
		if (OldMax > 0.f) {
			const float Percent = GetHealth() / OldMax;
			SetHealth(FMath::Clamp(Percent * NewValue, 0.f, NewValue));
		}	else {
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
		}	else {
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
		}	else {
			SetMorale(FMath::Clamp(GetMorale(), 0.f, NewValue));
		}
	}
}

// Post effect override helps clamp to max attributes
void USAFAttributes::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) {
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));
	}	else if (Data.EvaluatedData.Attribute == GetShieldsAttribute())	{
		SetShields(FMath::Clamp(GetShields(), 0.f, GetMaxShields()));
	}	else if (Data.EvaluatedData.Attribute == GetMoraleAttribute())	{
		SetMorale(FMath::Clamp(GetMorale(), 0.f, GetMaxMorale()));
	}
}

// ===========================================================================
//                                Production
// ===========================================================================

// Returns the cost attributes as a FSAFResources bundle.
FSAFResources USAFAttributes::BundleResources() const {
	FSAFResources Out;

	const float C1  = Cost1.GetCurrentValue();
	const float C2  = Cost2.GetCurrentValue();
	const float C3  = Cost3.GetCurrentValue();
	const float C4  = Cost4.GetCurrentValue();
	const float C5  = Cost5.GetCurrentValue();
	const float C6  = Cost6.GetCurrentValue();
	const float C7  = Cost7.GetCurrentValue();
	const float C8  = Cost8.GetCurrentValue();
	const float C9  = Cost9.GetCurrentValue();
	const float C10 = Cost10.GetCurrentValue();
	const float C11 = Cost11.GetCurrentValue();
	const float C12 = Cost12.GetCurrentValue();

	Out.Resource1  = FSAFResources::ToInt(C1);
	Out.Resource2  = FSAFResources::ToInt(C2);
	Out.Resource3  = FSAFResources::ToInt(C3);
	Out.Resource4  = FSAFResources::ToInt(C4);
	Out.Resource5  = FSAFResources::ToInt(C5);
	Out.Resource6  = FSAFResources::ToInt(C6);
	Out.Resource7  = FSAFResources::ToInt(C7);
	Out.Resource8  = FSAFResources::ToInt(C8);
	Out.Resource9  = FSAFResources::ToInt(C9);
	Out.Resource10 = FSAFResources::ToInt(C10);
	Out.Resource11 = FSAFResources::ToInt(C11);
	Out.Resource12 = FSAFResources::ToInt(C12);

	return Out;
}

// ===========================================================================
//                                Replication
// ===========================================================================

void USAFAttributes::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Armour
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, FrontArmour,    		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, SideArmour,     		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, RearArmour,     		COND_None, REPNOTIFY_Always);

	// Defense
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Health,         		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, MaxHealth,      		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Shields,        		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, MaxShields,     		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Morale,         		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, MaxMorale,      		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Durability,     		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Defense,        		COND_None, REPNOTIFY_Always);

	// Combat
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Accuracy,       		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Evasion,        		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, WeaponHandling, 		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, WeaponSkill,    		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, CritChance,     		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, CritMultiplier, 		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Luck,           		COND_None, REPNOTIFY_Always);

	// Movement
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, MoveSpeed,      		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, SprintModifier, 		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Speed,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Agility,        		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, FormationSpacing,	COND_None, REPNOTIFY_Always);

	// Production
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, BuildTime,      		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, BuildSpeed,     		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost1,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost2,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost3,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost4,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost5,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost6,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost7,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost8,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost9,          		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost10,         		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost11,         		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, Cost12,         		COND_None, REPNOTIFY_Always);

	// Ranges
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, FieldOfView,    		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, DefenseRange,   		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, ChaseRange,     		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, AbilityRadius,  		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(USAFAttributes, EffectRadius,   		COND_None, REPNOTIFY_Always);
}
