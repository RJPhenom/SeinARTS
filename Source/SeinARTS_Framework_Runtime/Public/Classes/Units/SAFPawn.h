#pragma once

#include "CoreMinimal.h"
#include "Classes/Units/SAFUnit.h"
#include "GameFramework/Pawn.h"
#include "Interfaces/Units/SAFPawnInterface.h"
#include "Resolvers/SAFAssetResolver.h"
#include "SAFPawn.generated.h"

class UCapsuleComponent;
class USkeletalMeshComponent;
class USAFMovementComponent;
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
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFPawn : public APawn, public ISAFPawnInterface {
	GENERATED_BODY()

public:

	ASAFPawn(const FObjectInitializer& ObjectInitializer);

	// Configuration
	// ==========================================================================================
	/** 
	 * Configure this pawn from a data asset.
	 * This sets up the mesh, animations, collision, movement component, 
     * and all movement parameters.
	 */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Pawn")
	void ConfigureFromAsset(USAFPawnAsset* InPawnAsset);

	// Components
	// ==========================================================================================
	/** Get the capsule collision component. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Pawn")
	UCapsuleComponent* GetCapsuleComponent() const { return CapsuleComponent; }

	/** Get the skeletal mesh component. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Pawn")
	USkeletalMeshComponent* GetMeshComponent() const { return MeshComponent; }

	/** Get the movement component (returns base class, cast to specific type as needed). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Pawn")
	USAFMovementComponent* GetSAFMovementComponent() const { return SAFMovementComponent; }

	// APawn Overrides
	// =================================================================================================================================
	virtual UPawnMovementComponent* GetMovementComponent() const override;
	virtual void 					BeginPlay() override;
	virtual void 					SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// Pawn Interface Implementation
	// =================================================================================================================================
	virtual void            		InitPawn_Implementation(USAFPawnAsset* InPawnAsset, ASAFUnit* InOwningUnit);
	virtual void            		SetOwningUnit_Implementation(ASAFUnit* InOwningUnit);
	virtual ASAFUnit*       		GetOwningUnit_Implementation() const                                    { return OwningUnit.Get(); }
	virtual bool            		HasOwningUnit_Implementation() const  { return OwningUnit && !OwningUnit->IsActorBeingDestroyed(); }
	virtual USAFPawnAsset*  		GetPawnAsset_Implementation() const            { return SAFAssetResolver::ResolveAsset(PawnAsset); }

    // Simple helpers / getters
	// =================================================================================================================================
    FVector                 		GetPawnVelocity() const;
    float                   		GetPawnSpeed() const                                            { return GetPawnVelocity().Size(); }
    bool                    		IsPawnMoving() const                                                { return GetPawnSpeed() > .1f; }

protected:

	// Core Components
	// =================================================================================================================
	/** Collision capsule for this pawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<UCapsuleComponent> CapsuleComponent;

	/** Skeletal mesh component for visual representation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USkeletalMeshComponent> MeshComponent;

	/** Movement component (type determined by PawnAsset). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	TObjectPtr<USAFMovementComponent> SAFMovementComponent;

	// Configuration
	// =================================================================================================================
	/** The data asset used to configure this pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Configuration")
	TSoftObjectPtr<USAFPawnAsset> PawnAsset;

	/** Reference to the unit that owns and manages this pawn */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SeinARTS|Unit")
	TObjectPtr<ASAFUnit> OwningUnit;

	// Helper Methods
	// =================================================================================================================
	/** Apply visual configuration from the pawn asset. */
	void ApplyVisualConfiguration();

	/** Apply collision configuration from the pawn asset. */
	void ApplyCollisionConfiguration();

	/** Create and configure the movement component from the pawn asset. */
	void CreateMovementComponent();

	/** Apply movement configuration to the movement component. */
	void ApplyMovementConfiguration();

};