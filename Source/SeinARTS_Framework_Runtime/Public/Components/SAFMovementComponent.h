#pragma once

#include "CoreMinimal.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/EngineTypes.h"
#include "SAFMovementComponent.generated.h"

/**
 * USAFMovementComponent
 *
 * Base movement component for all SeinARTS Framework units.
 * Provides common functionality for AI movement, navigation mesh integration,
 * and basic movement properties. Inherits from UFloatingPawnMovement to provide
 * a lightweight alternative to CharacterMovementComponent for RTS units.
 *
 * This serves as the foundation for both infantry and vehicle movement systems,
 * supporting AI MoveTo requests and providing consistent movement behavior
 * across different unit types.
 */
UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFMovementComponent : public UFloatingPawnMovement {
	GENERATED_BODY()

public:

	USAFMovementComponent();

	// Core Movement Properties
	// ==================================================================================================
	// Note: MaxSpeed, Acceleration, and Deceleration are inherited from UFloatingPawnMovement
	/** Maximum rotation rate (degrees per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Movement", meta=(ClampMin="0"))
	float MaxRotationRate = 720.0f;

	// Navigation Properties
	// ==================================================================================================
	/** Whether to project movement to navigation mesh to prevent units from falling off nav edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Navigation")
	bool bProjectToNavMesh = true;

	/** Extent used for navigation mesh projection queries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Navigation", 
    meta=(EditCondition="bProjectToNavMesh"))
	FVector NavProjectionExtent = FVector(100.0f, 100.0f, 500.0f);

	/** Threshold below which the unit is considered to have stopped (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Movement", meta=(ClampMin="0"))
	float StopSpeedThreshold = 1.0f;

	// AI Movement Interface
	// ==================================================================================================
	/** AI pathfinding entry point - called by AI controllers to request movement. */
	virtual void RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) override;
	
	/** Stop all active movement immediately. */
	virtual void StopActiveMovement() override;

	/** Returns true if the unit is currently moving. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual bool IsMoving() const;

	/** Returns true if the unit has been requested to move (may be turning in place). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual bool HasMoveRequest() const { return bHasMoveRequest; }

	/** Get the current desired movement direction (normalized). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual FVector GetDesiredMoveDirection() const { return DesiredMoveDirection; }

	/** Get the current desired movement speed. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual float GetDesiredMoveSpeed() const { return DesiredMoveSpeed; }

protected:

	// Movement State
	// ==================================================================================================
	/** Whether a move request is currently active. */
	bool bHasMoveRequest = false;

	/** The desired movement direction (normalized, in world space). */
	FVector DesiredMoveDirection = FVector::ZeroVector;

	/** The desired movement speed (cm/s). */
	float DesiredMoveSpeed = 0.0f;

	// Core Tick Function
	// ==================================================================================================
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Movement Implementation - Override in derived classes
	// ==================================================================================================
	/** Apply movement logic for this frame. Called from TickComponent. */
	virtual void PerformMovement(float DeltaTime);

	/** Apply rotation logic for this frame. Called from TickComponent. */
	virtual void PerformRotation(float DeltaTime);

	// Utility Functions
	// ==================================================================================================
	/** Project a world position to the navigation mesh if projection is enabled. */
	virtual bool ProjectToNavMesh(const FVector& WorldLocation, FVector& OutProjectedLocation) const;

	/** Apply movement with optional navigation mesh projection. */
	virtual void SafePerformMovement(const FVector& Delta, const FRotator& NewRotation, float DeltaTime);

	/** Get the current forward direction of the pawn. */
	virtual FVector GetForwardDirection() const;

	/** Get the current rotation of the pawn. */
	virtual FRotator GetCurrentRotation() const;

	/** Calculate the desired rotation based on movement direction. */
	virtual FRotator CalculateDesiredRotation() const;

	/** Constrain a vector to XY plane if bConstrainToXYPlane is true. */
	virtual FVector ConstrainToPlane(const FVector& Vector) const;

};