#pragma once

#include "CoreMinimal.h"
#include "SAFMovementModes.generated.h"

/**
 * ESAMovementMode
 *
 * Enumerates the different MovementModes available to use for the
 * SAFMovementComponent.
 */
UENUM(BlueprintType)
enum class ESAFMovementMode : uint8 {
	Infantry    UMETA(DisplayName="Infantry"),
	Tracked     UMETA(DisplayName="Tracked"),
	Wheeled     UMETA(DisplayName="Wheeled"),
	Hover       UMETA(DisplayName="Hover")
};