#pragma once

#include "CoreMinimal.h"
#include "SAFCoverTypes.generated.h"

/**
 * ESAFCoverType
 * 
 * Enumerates the different levels of cover in the SeinARTS Framework
 * cover system.
 */
UENUM(BlueprintType)
enum class ESAFCoverType : uint8 {
	Negative UMETA(DisplayName="Negative"),
	None     UMETA(DisplayName="None"),
	Light    UMETA(DisplayName="Light"),
	Heavy    UMETA(DisplayName="Heavy")
};