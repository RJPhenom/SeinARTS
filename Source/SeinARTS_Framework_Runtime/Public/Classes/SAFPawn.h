#pragma once

#include "CoreMinimal.h"
#include "Classes/SAFUnit.h"
#include "Components/SAFMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/SAFPawnInterface.h"
#include "Resolvers/SAFAssetResolver.h"
#include "SAFPawn.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class USAFPawnAsset;

/**
 * ASAFPawn
 * 
 * Completely agnostic pawn class for the SeinARTS Framework that can represent any unit type.
 * This class is configured entirely through a USAFPawnAsset at runtime, with no hardcoded
 * assumptions about unit type, behavior, or appearance.
 * 
 * The pawn type (infantry, vehicle, aircraft, etc.) is determined by:
 * - The MovementComponentClass specified in the PawnAsset
 * - The SkeletalMesh and animations used
 * - The movement parameters configured
 * 
 * This replaces the need for separate ASAFSquadMember and ASAFVehiclePawn classes.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Pawn"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFPawn : public APawn, 
	public ISAFPawnInterface,
	public ISAFActorInterface {

	GENERATED_BODY()

public:

	ASAFPawn(const FObjectInitializer& ObjectInitializer);

	// Components
	// ==========================================================================================
	/** Get the capsule collision component. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Pawn")
	UCapsuleComponent* GetPawnCapsule() const { return PawnCapsule; }

	/** Get the skeletal mesh component. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Pawn")
	USkeletalMeshComponent* GetPawnMesh() const { return PawnMesh; }

	/** Get the movement component (returns base class, cast to specific type as needed). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Pawn")
	USAFMovementComponent* GetPawnMovement() const { return PawnMovement; }
	
	// Actor Interface Overrides
	// =================================================================================================================================
	virtual USAFAsset* 				GetAsset_Implementation() const; /** Gets the owner asset */
	virtual void 					InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize);

	virtual ASAFPlayerState* 		GetOwningPlayer_Implementation() const;

	virtual bool 					GetMultiSelectable_Implementation() const;
	virtual void 					SetMultiSelectable_Implementation(bool bNewMultiSelectable);
	virtual bool 					Select_Implementation(AActor*& OutSelectedActor);
	virtual bool 					QueueSelect_Implementation(AActor*& OutQueueSelectedActor);
	virtual void 					Deselect_Implementation();
	virtual void 					DequeueSelect_Implementation();

	// Pawn Interface Implementation
	// =================================================================================================================================
	virtual void            		InitPawn_Implementation(USAFPawnAsset* InPawnAsset, ASAFUnit* InOwningUnit);
	virtual USAFPawnAsset*  		GetPawnAsset_Implementation() const            { return SAFAssetResolver::ResolveAsset(PawnAsset); }

	virtual void            		SetOwningUnit_Implementation(ASAFUnit* InOwningUnit) 				  { OwningUnit = InOwningUnit; }
	virtual ASAFUnit*       		GetOwningUnit_Implementation() const                                    { return OwningUnit.Get(); }
	virtual bool            		HasOwningUnit_Implementation() const  { return OwningUnit && !OwningUnit->IsActorBeingDestroyed(); }

    // Simple helpers / getters
	// =================================================================================================================================
    FVector                 		GetPawnVelocity() const;
    float                   		GetPawnSpeed() const                                            { return GetPawnVelocity().Size(); }
    bool                    		IsPawnMoving() const                                                { return GetPawnSpeed() > .1f; }

protected:

	virtual void PostInitializeComponents() override;

	// Core Components
	// =================================================================================================================
	/** Collision capsule for this pawn. */
	UPROPERTY(EditAnywhere,BlueprintReadOnly, Category="Components")
	TObjectPtr<UCapsuleComponent> PawnCapsule;

	/** Skeletal mesh component for visual representation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> PawnMesh;

	/** Movement component (type determined by PawnAsset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USAFMovementComponent> PawnMovement;

	// Configuration
	// =================================================================================================================
	/** The data asset used to configure this pawn. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category="SeinARTS")
	TSoftObjectPtr<USAFPawnAsset> PawnAsset;

	/** Reference to the unit that owns and manages this pawn */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing=OnRep_OwningUnit, Category="SeinARTS")
	TObjectPtr<ASAFUnit> OwningUnit;

	// Private Helpers
	// =================================================================================================================
	/** Apply collision configuration from the pawn asset. */
	void ApplyCapsuleConfiguration();

	/** Apply visual configuration from the pawn asset. */
	void ApplyVisualConfiguration();

	/** Apply movement configuration to the movement component. */
	void ApplyMovementConfiguration();

	/** Sync the navigation agent's capsule size with the pawn's capsule component. */
	void SyncNavAgentWithCapsule();
	
	// Replication
	// ==================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION()	void OnRep_OwningUnit();

};