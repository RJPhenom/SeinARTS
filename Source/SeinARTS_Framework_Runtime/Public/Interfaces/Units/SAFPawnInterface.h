#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SAFPawnInterface.generated.h"

class ASAFUnit;
class USAFPawnAsset;

UINTERFACE(Blueprintable)
class USAFPawnInterface : public UInterface { GENERATED_BODY() };

/**
 * SAFPawnInterface
 * 
 * The unified interface for pawn actors that represent units in the SeinARTS Framework.
 * This consolidates the functionality previously split between SAFSquadMemberInterface 
 * and SAFVehiclePawnInterface, allowing any pawn to serve as a unit representation
 * regardless of the specific unit type (Squad, Vehicle, etc.).
 */
class SEINARTS_FRAMEWORK_RUNTIME_API ISAFPawnInterface {

	GENERATED_BODY()

public:

	/** Initializes the pawn with its data asset and owning unit. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Pawn")
	void InitPawn(USAFPawnAsset* InPawnAsset, ASAFUnit* InOwningUnit);
	
	/** Get the data asset that defines this pawn's properties. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Pawn")
	USAFPawnAsset* GetPawnAsset() const;

	/** Sets the owning unit for this pawn (server-authoritative). */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Pawn")
	void SetOwningUnit(ASAFUnit* InOwningUnit);

	/** Gets the pawn's owning unit, if any. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Pawn")
	ASAFUnit* GetOwningUnit() const;

	/** Returns true if the pawn is currently assigned to a unit. */
	UFUNCTION(BlueprintNativeEvent, Category="SeinARTS|Pawn")
	bool HasOwningUnit() const;

};