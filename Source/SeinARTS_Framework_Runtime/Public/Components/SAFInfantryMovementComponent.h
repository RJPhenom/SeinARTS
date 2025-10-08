#pragma once

#include "CoreMinimal.h"
#include "Components/SAFMovementComponent.h"
#include "Engine/EngineTypes.h"
#include "SAFInfantryMovementComponent.generated.h"

/**
 * USAFInfantryMovementComponent
 *
 * Infantry-specific movement component designed for RTS-style squad-based movement.
 * Inspired by Company of Heroes and Dawn of War, providing responsive infantry movement
 * with formation support, unit separation, and tactical movement features.
 * 
 * Features:
 * - Responsive acceleration/deceleration for quick formation changes
 * - Unit separation to prevent overlap
 * - Strafe movement and facing control for tactical positioning
 * - Formation-aware movement for squad cohesion
 */
UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFInfantryMovementComponent : public USAFMovementComponent {
	GENERATED_BODY()

public:

	USAFInfantryMovementComponent();

	// Infantry Movement Properties
	// ==================================================================================================
	
	/** Braking friction factor for quick stops (higher = more responsive). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement", meta=(ClampMin="0"))
	float BrakingFriction = 8.0f;

	/** Whether this unit can strafe (move without changing facing direction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement")
	bool bAllowStrafe = true;

	// Facing Control
	// ==================================================================================================
	
	/** Whether to use a specific desired facing direction instead of movement direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Facing")
	bool bUseDesiredFacing = false;

	/** Desired facing direction (yaw in degrees). Only used if bUseDesiredFacing is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Facing")
	float DesiredFacingYaw = 0.0f;

	/** How fast the unit rotates to face the desired direction (degrees per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Facing", meta=(ClampMin="1"))
	float FacingRotationRate = 720.0f;

	// Unit Separation
	// ==================================================================================================
	
	/** Enable separation behavior to prevent units from overlapping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Separation")
	bool bEnableSeparation = true;

	/** Distance at which separation forces begin to apply (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Separation", meta=(ClampMin="0"))
	float SeparationRadius = 120.0f;

	/** Strength of separation forces (cm/s²). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Separation", meta=(ClampMin="0"))
	float SeparationStrength = 2000.0f;

	/** Maximum speed at which separation can push units (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Separation", meta=(ClampMin="0"))
	float MaxSeparationSpeed = 400.0f;

	/** Collision channel used for separation queries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Separation")
	TEnumAsByte<ECollisionChannel> SeparationChannel = ECC_Pawn;

	// Formation Support
	// ==================================================================================================
	
	/** Enable formation-aware movement (slower when not in formation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Formation")
	bool bUseFormationMovement = true;

	/** Speed multiplier when moving out of formation (0.0-1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Formation", 
		meta=(ClampMin="0.1", ClampMax="1.0", EditCondition="bUseFormationMovement"))
	float OutOfFormationSpeedMultiplier = 0.7f;

	/** Distance from desired formation position considered "in formation" (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Formation", 
		meta=(ClampMin="0", EditCondition="bUseFormationMovement"))
	float FormationTolerance = 100.0f;

	// Blueprint Interface
	// ==================================================================================================
	
	/** Set the desired facing direction (in world yaw degrees). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void SetDesiredFacing(float InYaw);

	/** Clear desired facing and return to movement-based facing. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void ClearDesiredFacing();

	/** Returns true if the unit is currently in formation. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Infantry Movement")
	bool IsInFormation() const { return bIsInFormation; }

	/** Set the desired formation position for this unit. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void SetFormationPosition(const FVector& InFormationPosition);

	/** Clear the formation position. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void ClearFormationPosition();

protected:

	// Formation state
	bool bHasFormationPosition = false;
	bool bIsInFormation = true;
	FVector FormationPosition = FVector::ZeroVector;

	// Override base movement implementation for infantry-specific behavior
	virtual void PerformMovement(float DeltaTime) override;
	virtual void PerformRotation(float DeltaTime) override;

	// Infantry-specific movement functions
	virtual void ApplyInfantryMovement(float DeltaTime);
	virtual void ApplySeparation(FVector& OutAcceleration, float DeltaTime);
	virtual void ApplyFormationLogic(float DeltaTime);

	// Movement calculations
	virtual FVector CalculateMovementAcceleration(const FVector& InputDirection, float DeltaTime) const;
	virtual float CalculateCurrentMaxSpeed() const;

	// Utility functions
	virtual FVector GetInputDirection() const;
	virtual bool ShouldApplyBraking() const;

};
