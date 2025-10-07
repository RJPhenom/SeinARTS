#pragma once

#include "CoreMinimal.h"
#include "SAFResourceRoundingPolicies.generated.h"

/**
 * ESAFResourceRoundingPolicy
 * 
 * Enumerates the different methods of rounding resources when 
 * assessing cost bundles.
 */
UENUM(BlueprintType)
enum class ESAFResourceRoundingPolicy : uint8 {
	Floor UMETA(DisplayName="Floor"),
	Round UMETA(DisplayName="Round"),
	Ceil  UMETA(DisplayName="Ceil")
};
