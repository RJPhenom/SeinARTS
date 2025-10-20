#pragma once

#include "CoreMinimal.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/EngineTypes.h"
#include "Enums/SAFMovementModes.h"
#include "SAFMovementComponent.generated.h"

/**
 * USAFMovementComponent
 *
 * Unified movement component class for SeinARTS Framework units.
 * 
 * Provides movement functionality for different unit types including infantry, tracked vehicles,
 * wheeled vehicles, and hover units. Uses a movement mode enum to determine which movement
 * behavior to use and shows only relevant properties in the editor based on the selected mode.
 * Inherits from UFloatingPawnMovement to provide a lightweight alternative to CharacterMovementComponent.
 */
UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable, 
meta=(BlueprintSpawnableComponent, DisplayName="SeinARTS Movement Component"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFMovementComponent : public UFloatingPawnMovement {
	GENERATED_BODY()

public:

	USAFMovementComponent();

	// AI Movement Interface
	// ==================================================================================================
	virtual void 		RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) 	override;
	virtual void 		StopActiveMovement() 													override;

	// Shared Movement Properties
	// ==================================================================================================
	/** Determines which movement behavior this component uses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	ESAFMovementMode MovementMode = ESAFMovementMode::Infantry;

	/** Maximum rotation rate (degrees per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS", meta=(ClampMin="0"))
	float MaxRotationRate = 300.0f;

	/** Whether to project movement to navigation mesh to prevent units from falling off nav edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS")
	bool bProjectToNavMesh = true;

	/** Extent used for navigation mesh projection queries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS", meta=(EditCondition="bProjectToNavMesh"))
	FVector NavProjectionExtent = FVector(100.0f, 100.0f, 500.0f);

	/** If a movement point is below this Dot threshold and within the Distance threshold, 
	 * then reverse to that point rather than rotate and move to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Wheeled", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="-1.0", ClampMax="1.0"))
	float ReverseEngageDotThreshold;

	/** If a movement point is within this Distance threshold and below the Dot threshold, 
	 * then reverse to that point rather than rotate and move to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Wheeled", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="0"))
	float ReverseEngageDistanceThreshold;

	/** Max speed when reversing, differs from overall max speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Wheeled", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="0"))
	float ReverseMaxSpeed;

	/** Threshold below which the unit is considered to have stopped (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS", meta=(ClampMin="0"))
	float StopSpeedThreshold = 1.0f;

	// Infantry Mode Properties
	// ==================================================================================================
	/** Whether this unit can strafe (move without changing facing direction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry", EditConditionHides))
	bool Infantry_bAllowStrafe;
	
	/** Dot product threshold by which the unit can no longer strafe, and must turn away from its target
	 * to keep moving in the desired direction (0 = directly to the left/right, defaults to 0.3). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry", EditConditionHides))
	float Infantry_StrafingDotThreshold;

	// Tracked Mode Properties
	// ==================================================================================================
	/** Curve representing forward throttle vs. direction misalignment for tracked mode. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Tracked", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked", EditConditionHides))
	FRuntimeFloatCurve Tracked_ThrottleVsMisalignmentDeg;

	// Wheeled Mode Properties
	// ==================================================================================================
	/** Represents the distance between axels (cm), affects turn radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Wheeled", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="1"))
	float Wheeled_Wheelbase;

	/** Maximum front wheel steering angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Wheeled", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="1", ClampMax="60"))
	float Wheeled_MaxSteerAngleDeg;

	/** How fast we approach desired steer angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Wheeled", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="0.1", ClampMax="10"))
	float Wheeled_SteerResponse;

	// Movement Blueprint API
	// ==================================================================================================
	/** Apply default values based on the selected movement mode. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Movement")
	void ApplyMovementModeDefaults();

	/** Returns true if the unit is currently moving. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual bool IsMoving() const
	{ return Velocity.SizeSquared() > FMath::Square(StopSpeedThreshold); }

	/** Returns true if the unit has been requested to move (may be turning in place). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual bool HasMoveRequest() const { return bHasMoveRequest; }

	/** Get the current desired movement direction (normalized). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual FVector GetDesiredMoveDirection() const { return DesiredMoveDirection; }

	/** Get the current desired movement speed. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Movement")
	virtual float GetDesiredMoveSpeed() const { return DesiredMoveSpeed; }

	/** Set the maximum rotation rate for this movement component. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Movement")
	virtual void SetMaxRotationRate(float NewMaxRotationRate)
	{ MaxRotationRate = FMath::Max(0.0f, NewMaxRotationRate); }

	/** Set the desired facing direction (in world yaw degrees). Infantry mode only. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void SetDesiredFacing(float InYaw) { DesiredFacingYaw = InYaw; bUseDesiredFacing = true; }

	/** Clear desired facing and return to movement-based facing. Infantry mode only. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void ClearDesiredFacing() { bUseDesiredFacing = false;}

	/** Returns true if the unit is currently in formation. Infantry mode only. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Infantry Movement")
	bool IsInFormation() const { return bIsInFormation; }

	/** Set the desired formation position for this unit. Infantry mode only. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void SetFormationPosition(const FVector& InFormationPosition) 
	{ FormationPosition = InFormationPosition; bHasFormationPosition = true; }

	/** Clear the formation position. Infantry mode only. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void ClearFormationPosition() 
	{ bHasFormationPosition = false; bIsInFormation = true; }

	/** Sets the world-space goal used by the "close & behind" auto-reverse rule.. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Vehicle Movement")
	void SetReverseCheckGoal(const FVector& InGoal) 
	{ bHasReverseGoal = true; ReverseGoal = InGoal; }

	/** Clears the auto-reverse goal so regular MoveTo will no longer reverse due to 
	 * the "close & behind" rule. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Vehicle Movement")
	void ClearReverseCheckGoal() { bHasReverseGoal = false; }

protected:

	// Movement State Variables
	// ==================================================================================================
	/** Whether a move request is currently active. */
	bool bHasMoveRequest = false;

	/** Whether to use a specific desired facing direction instead of movement direction. */
	bool bUseDesiredFacing = false;

	/** Desired facing direction (yaw in degrees). Only used if bUseDesiredFacing is true. */
	float DesiredFacingYaw = 0.0f;

	/** The desired movement direction (normalized, in world space). */
	FVector DesiredMoveDirection = FVector::ZeroVector;

	/** The desired movement speed (cm/s). */
	float DesiredMoveSpeed = 0.0f;

	/** Formation state for infantry. */
	bool bHasFormationPosition = false;

	/** Whether the unit is currently in formation. */
	bool bIsInFormation = true;

	/** Desired formation position in world space. */
	FVector FormationPosition = FVector::ZeroVector;

	/** Vehicle-specific state (different from base class movement state). */
	bool bHasReverseGoal = false;

	/** Current steering angle (in degrees). */
	float CurrentSteerDeg = 0.0f;

	/** Reverse goal position in world space. */
	FVector ReverseGoal = FVector::ZeroVector;

	// Core Tick / Movement Functions
	// ==================================================================================================
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	void PerformMovement(float DeltaTime);
	void PerformBaseMovement(float DeltaTime);
	void PerformInfantryMovement(float DeltaTime);
	void PerformTrackedMovement(float DeltaTime);
	void PerformWheeledMovement(float DeltaTime);
	void PerformHoverMovement(float DeltaTime);

	// Utility Functions
	// ==================================================================================================
	/** Evaluate the tracked throttle based on misalignment. */
	float EvalTrackedThrottle(float MisalignmentDeg) const;

	/** Apply forward velocity movement based on the current state. */
	void ApplyForwardVelocityMovement(float DeltaTime, FRotator NewRot, bool bUseReverse, float TargetSpeedUnsigned);

	/** Get the current forward direction of the pawn. */
	virtual FVector GetForwardDirection() const
	{ return UpdatedComponent ? UpdatedComponent->GetForwardVector() : FVector::ForwardVector; }

	/** Get the current rotation of the pawn. */
	virtual FRotator GetCurrentRotation() const
	{ return UpdatedComponent ? UpdatedComponent->GetComponentRotation() : FRotator::ZeroRotator; }

	/** Calculate the desired rotation based on movement direction. */
	virtual FRotator CalculateDesiredRotation() const
	{ return DesiredMoveDirection.IsNearlyZero() ? GetCurrentRotation() : DesiredMoveDirection.Rotation(); }

	/** Constrain a vector to XY plane if bConstrainToXYPlane is true. */
	virtual FVector ConstrainToPlane(const FVector& Vector) const
	{ return bConstrainToPlane ? FVector(Vector.X, Vector.Y, 0.0f) : Vector; }

	#if WITH_EDITOR
	/** Reapply mode defaults when movement mode changes in the editor */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif

};