#pragma once

#include "CoreMinimal.h"
#include "SAFVehicleDriveTypes.generated.h"

/**
 * ESAFVehicleDriveType
 * 
 * Enumerates the different VehicleDriveTypes available to use for the
 * SAFVehicleMovement component.
 */
UENUM(BlueprintType)
enum class ESAFVehicleDriveType : uint8 {
	Tracked     UMETA(DisplayName="Tracked"),
	Wheeled     UMETA(DisplayName="Wheeled"),
	Hover       UMETA(DisplayName="Hover")
};