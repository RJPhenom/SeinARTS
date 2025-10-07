#pragma once

#include "CoreMinimal.h"
#include "SAFArmourTypes.generated.h"

/**
 * ESAFArmourType
 * 
 * Enumerates the different levels of armour in the SeinARTS Framework
 * unit attributes set.
 */
UENUM(BlueprintType)
enum class ESAFArmourType : uint8 {
	None 			UMETA(DisplayName="None"),
	Light  		UMETA(DisplayName="Light"),
	Medium   	UMETA(DisplayName="Medium"),
	Heavy    	UMETA(DisplayName="Heavy")
};