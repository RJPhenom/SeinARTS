#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SAFVehiclePawnInterface.generated.h"

class ASAFVehicle;
class USAFVehicleAsset;

/**
 * SAFVehicleInterface
 * 
 * The SAFVehiclePawnInterface is the interface that designates and enables a pawn class to act as
 * a vehicle unit's representation in the game world.
 */
UINTERFACE(Blueprintable) class USAFVehiclePawnInterface : public UInterface { GENERATED_BODY() };
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFVehiclePawnInterface {

	GENERATED_BODY()

public:

	/** Initializes the squad member actor. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Vehicle")
	void InitVehiclePawn(USAFVehicleAsset* InVehicleAsset, ASAFVehicle* InVehicle);

	/** Returns a reference to the parent Vehicle unit's data asset. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Vehicle")
	USAFVehicleAsset* GetVehicleAsset() const;

	/** Returns a reference to the parent Vehicle unit. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Vehicle")
	ASAFVehicle* GetVehicle() const;

	/** Sets the parent Vehicle unit for this pawn. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Vehicle")
	void SetVehicle(ASAFVehicle* InVehicle);
};
