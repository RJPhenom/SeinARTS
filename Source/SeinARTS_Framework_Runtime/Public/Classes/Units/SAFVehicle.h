#pragma once

#include "CoreMinimal.h"
#include "Classes/SAFUnit.h"
#include "Assets/Units/SAFVehicleAsset.h"
#include "Data/SAFVehicleData.h"
#include "SAFVehicle.generated.h"

class APawn;

UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Vehicle Unit"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFVehicle : public ASAFUnit {
	GENERATED_BODY()

public:

	ASAFVehicle();
	USAFVehicleAsset* 	GetVehicleAsset() { return Cast<USAFVehicleAsset>(SAFAssetResolver::ResolveAsset(Asset)); }

	// Asset Interface Overrides
	// ======================================================================================================================================
	virtual void 				InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) 		override;

	// Vehicle API
	// ======================================================================================================================================
	/** Contains references to the pawn this unit class governs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRep_VehiclePawn, Category="SeinARTS|Vehicle")
	TObjectPtr<APawn> VehiclePawn;

	/** Contains a reference to the VehiclePawn class you are using to represent this vehicle unit in the world. 
	Defaults to SAFVehiclePawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="SeinARTS|Vehicle")
	TSubclassOf<APawn> VehiclePawnClass;

	/** Initializes the vehicle (generates members and positions) */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="SeinARTS|Squad")
	void InitVehicle(USAFVehicleAsset* VehicleAsset);
	virtual void InitVehicle_Implementation(USAFVehicleAsset* VehicleAsset);

	/** Get this vehicle unit's pawn. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Vehicle")
	APawn* GetVehiclePawn() const { return VehiclePawn.Get(); }

protected:

	/** Init flag to prevent duplicates on listen servers */
	bool bInitialized = false;

	// Replication
	// ==============================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION()	void OnRep_VehiclePawn();

};
