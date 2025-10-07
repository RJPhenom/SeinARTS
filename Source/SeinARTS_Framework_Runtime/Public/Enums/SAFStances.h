#pragma once

#include "CoreMinimal.h"
#include "SAFStances.generated.h"

/**
 * ESAFStance
 * 
 * Enumerates the different stances units can take in the SeinARTS Framework
 */
UENUM(BlueprintType)
enum class ESAFStance : uint8 {
	Aggressive  UMETA(DisplayName="Aggressive"),
	Defensive   UMETA(DisplayName="Defensive"),
	StandGround UMETA(DisplayName="Stand Ground"),
	HoldFire    UMETA(DisplayName="Hold Fire")
};