#pragma once

#include "CoreMinimal.h"
#include "Assets/SAFAsset.h"
#include "Engine/EngineTypes.h"
#include "Enums/SAFMovementModes.h"
#include "SAFPawnAsset.generated.h"

class USkeletalMesh;
class UAnimInstance;
class UMaterialInterface;
class USAFMovementComponent;

/**
 * SAFPawnAsset
 * 
 * Completely agnostic data asset for defining pawn properties in the SeinARTS Framework.
 * This asset defines everything needed to configure a pawn at runtime:
 * - Visual representation (mesh, animations)
 * - Collision properties (capsule dimensions)
 * - Movement component type and settings
 * - All movement parameters
 * 
 * The pawn type (infantry vs vehicle vs aircraft) is determined by the MovementComponentClass
 * and mesh, making the system completely data-driven.
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Pawn Asset"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFPawnAsset : public USAFAsset {
	GENERATED_BODY()

public:

	USAFPawnAsset(const FObjectInitializer& ObjectInitializer);

	// Visual Configuration
	// =================================================================================================================
	/** Skeletal mesh to use for the pawn's visual representation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual Configuration")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Animation Blueprint class to drive the pawn's animations. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual Configuration")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Transform offset for positioning the mesh relative to the pawn's collision capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual Configuration")
	FTransform MeshOffset = FTransform::Identity;

	// Capsule Configuration
	// =================================================================================================================
	/** Radius of the collision capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Capsule Configuration")
	float CapsuleRadius = 50.0f;

	/** Half-height of the collision capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Capsule Configuration")
	float CapsuleHalfHeight = 100.0f;

	// Movement Configuration
	// =================================================================================================================
	/**Default movement mode of this pawn. This can be changed at runtime, so this setting only effects the movement
	 * mode set after initialization. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	ESAFMovementMode MovementMode;
	
	/** Maximum movement speed for this pawn (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float MaxSpeed = 250.0f;

	/** Acceleration rate for movement (cm/s²). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float Acceleration = 1500.0f;

	/** Deceleration rate when stopping (cm/s²). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float Deceleration = 2000.0f;

	/** Maximum rotation rate (degrees per second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float MaxRotationRate = 300.0f;

	/** Turning boost multiplier (0 = no turning boost, higher = more responsive turning). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float TurningBoost = 0.0f;

	/** Whether to constrain movement to a plane (typically XY for RTS units). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	bool bConstrainToPlane = true;

	/** If constraining to plane, this defines the plane normal (typically up vector for XY plane). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", meta=(EditCondition="bConstrainToPlane", EditConditionHides))
	FVector PlaneConstraintNormal = FVector::UpVector;

	/** Whether to snap to the constraint plane at spawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", meta=(EditCondition="bConstrainToPlane", EditConditionHides))
	bool bSnapToPlaneAtStart = true;

	// Navigation Properties
	// =============================================================================================================================================
	/** Whether to project movement to navigation mesh to prevent units from falling off nav edges. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	bool bProjectToNavMesh = true;

	/** Extent used for navigation mesh projection queries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", meta=(EditCondition="bProjectToNavMesh", EditConditionHides))
	FVector NavProjectionExtent = FVector(100.0f, 100.0f, 500.0f);

	/** If a movement point is below this Dot threshold and within the Distance threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked || MovementMode==ESAFMovementMode::Wheeled || MovementMode==ESAFMovementMode::Hover", 
	EditConditionHides, ClampMin="-1.0", ClampMax="1.0", DisplayName="Reverse Engage Dot Threshold"))
	float ReverseEngageDotThreshold = -0.5f;

	/** If a movement point is within this Distance threshold and below the Dot threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked || MovementMode==ESAFMovementMode::Wheeled || MovementMode==ESAFMovementMode::Hover", 
	EditConditionHides, ClampMin="0", DisplayName="Reverse Engage Distance Threshold"))
	float ReverseEngageDistanceThreshold = 2500.0f;

	/** Max speed when reversing, differs from overall max speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked || MovementMode==ESAFMovementMode::Wheeled || MovementMode==ESAFMovementMode::Hover", 
	EditConditionHides, ClampMin="0", DisplayName="Reverse Max Speed"))
	float ReverseMaxSpeed = 100.0f;

	/** Threshold below which the unit is considered to have stopped (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", meta=(ClampMin="0"))
	float StopSpeedThreshold = 1.0f;

	// Infantry Mode Properties
	// =============================================================================================================================================
	/** Whether this unit can strafe (move without changing facing direction). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry", EditConditionHides, DisplayName="Allow Strafing"))
	bool Infantry_bAllowStrafe = true;

	/** Dot product threshold by which the unit can no longer strafe, and must turn away from its target
	 * to keep moving in the desired direction (0 = directly to the left/right, defaults to 0.3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry && Infantry_bAllowStrafe", EditConditionHides, DisplayName="Strafing Dot Threshold"))
	float Infantry_StrafingDotThreshold = 0.3f;

	// Tracked Mode Properties
	// =================================================================================================================
	/** Curve representing forward throttle vs. direction misalignment for tracked vehicles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked", EditConditionHides, DisplayName="Throttle Vs Misalignment Deg"))
	FRuntimeFloatCurve Tracked_ThrottleVsMisalignmentDeg;

	// Wheeled Mode Properties
	// =================================================================================================================
	/** Represents the distance between axels (cm), affects turn radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="1", DisplayName="Wheelbase"))
	float Wheeled_Wheelbase = 220.0f;

	/** Maximum front wheel steering angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="1", ClampMax="60", DisplayName="Max Steer Angle Deg"))
	float Wheeled_MaxSteerAngleDeg = 60.0f;

	/** How fast we approach desired steer angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
	meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="0.1", ClampMax="10", DisplayName="Steer Response"))
	float Wheeled_SteerResponse = 3.0f;

protected:

	virtual void PostLoad() override;

	#if WITH_EDITOR
	/** Reapply mode defaults when movement mode changes in the editor */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif

};