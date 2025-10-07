#pragma once

#include "CoreMinimal.h"
#include "Classes/Units/SAFVehicle.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/SAFAssetInterface.h"
#include "Interfaces/Units/SAFVehiclePawnInterface.h"
#include "SAFVehiclePawn.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class USAFVehicleMovementComponent;

/* 
* SAFVehiclePawn 
*
* The class that provides a specialized pawn for representing the abstract SAFVehicle
* unit class.
*
* The SAFVehiclePawn inherits the SAFUnitInterface and is designed to forward calls
* to its governing Unit, the SAFSquad.
*/
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS VehiclePawn Unit"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFVehiclePawn : public APawn, 
	public ISAFAssetInterface, 
	public ISAFVehiclePawnInterface {
  GENERATED_BODY()

public: 

  ASAFVehiclePawn();

	// Asset Interface Overrides
	// ==========================================================================================================
	virtual USAFAsset* 				GetAsset_Implementation() const;
	virtual void 							InitAsset_Implementation(USAFAsset* InData, ASAFPlayerState* InOwner);
	
	virtual ASAFPlayerState* 	GetOwningPlayer_Implementation() const;

	virtual bool 							GetMultiSelectable_Implementation() const { return Vehicle.Get()->bMultiSelectable; }
	virtual void 							SetMultiSelectable_Implementation(bool bNewMultiSelectable);
	virtual bool 							Select_Implementation(AActor*& OutSelectedActor);
	virtual bool 							QueueSelect_Implementation(AActor*& OutQueueSelectedActor);
	virtual void 							Deselect_Implementation();
	virtual void 							DequeueSelect_Implementation();

	// Vehicle Pawn Interface / API
	// ==========================================================================================================
	virtual void 							InitVehiclePawn_Implementation(USAFVehicleAsset* InAsset, ASAFVehicle* InVehicle);
  virtual USAFVehicleAsset* GetVehicleAsset_Implementation() const;
	virtual ASAFVehicle* 			GetVehicle_Implementation() const { return Vehicle.Get(); }
  virtual void 							SetVehicle_Implementation(ASAFVehicle* InVehicle);

	/* Owning/Managing vehicle reference. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_Vehicle, Category="SeinARTS|Vehicle")
	TObjectPtr<ASAFVehicle> Vehicle;
  FORCEINLINE USkeletalMeshComponent* GetMesh() const { return VehicleMeshComponent; }

protected:

	virtual void BeginPlay() override;
  virtual void PostInitializeComponents() override;

  // The collider for this vehicle pawn.
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS|Vehicle")
  TObjectPtr<UCapsuleComponent> VehicleCapsuleComponent;

  // The visual mesh of the vehicle.
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS|Vehicle")
  TObjectPtr<USkeletalMeshComponent> VehicleMeshComponent;

  // The movement component that drives default SeinARTS Framework vehicles.
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS|Vehicle")
  TObjectPtr<USAFVehicleMovementComponent> VehicleMovementComponent;


	// Internal Helpers
	// ===========================================================
	// Applies the visuals from VehicleAsset to this pawn.
  void ApplyVisuals();

	// Syncs the nav agent properties with the capsule size.
	// (called on init and on capsule size change)
  void SyncNavAgentWithCapsule();

	// Replication
	// ===================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION()	void OnRep_Vehicle();

};