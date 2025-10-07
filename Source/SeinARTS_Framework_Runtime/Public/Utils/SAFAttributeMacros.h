#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"

// Shared macros for simplifying attribute set declarations
// ==================================================================================================

/**
 * SAF_ATTR_ACCESSORS
 * Expands to the standard GAS accessors for a UPROPERTY-backed attribute:
 * 	- GAMEPLAYATTRIBUTE_PROPERTY_GETTER
 * 	- GAMEPLAYATTRIBUTE_VALUE_GETTER
 * 	- GAMEPLAYATTRIBUTE_VALUE_SETTER
 * 	- GAMEPLAYATTRIBUTE_VALUE_INITTER
 */
#define SAF_ATTR_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * To declare a simple OnRep declaration.
 * Usage: SAF_ATTR_ONREP_DECL(Health)
 */
#define SAF_ATTR_ONREP_DECL(PropertyName) \
	UFUNCTION() void OnRep_##PropertyName(const FGameplayAttributeData& Old##PropertyName);
