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
	float MaxSpeed = 200.0f;

	/** Acceleration rate for movement (cm/s²). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float Acceleration = 1500.0f;

	/** Deceleration rate when stopping (cm/s²). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float Deceleration = 2000.0f;

	/** Maximum rotation rate (degrees per second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	float MaxRotationRate = 1440.0f;

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
	// =================================================================================================================
	/** Whether to project movement to navigation mesh to prevent units from falling off nav edges. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration")
	bool bProjectToNavMesh = true;

	/** Extent used for navigation mesh projection queries. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", meta=(EditCondition="bProjectToNavMesh", EditConditionHides))
	FVector NavProjectionExtent = FVector(100.0f, 100.0f, 500.0f);

	/** Threshold below which the unit is considered to have stopped (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", meta=(ClampMin="0"))
	float StopSpeedThreshold = 1.0f;

	// Infantry Mode Properties
	// =================================================================================================================
	
	/** Whether this unit can strafe (move without changing facing direction). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry", EditConditionHides, DisplayName="Infantry | Allow Strafe"))
	bool Infantry_bAllowStrafe = true;

	/** Whether to use a specific desired facing direction instead of movement direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry", EditConditionHides))
	bool Infantry_bUseDesiredFacing = false;

	/** Desired facing direction (yaw in degrees). Only used if bUseDesiredFacing is true. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry", EditConditionHides))
	float Infantry_DesiredFacingYaw = 0.0f;

	/** How fast the unit rotates to face the desired direction (degrees per second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Infantry", EditConditionHides, ClampMin="1"))
	float Infantry_FacingRotationRate = 1440.0f;

	// Tracked Mode Properties
	// =================================================================================================================

	/** Maximum turn rate in degrees for tracked vehicles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked", EditConditionHides))
	float Tracked_MaxTurnRateDeg = 60.0f;

	/** If a movement point is below this Dot threshold and within the Distance threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked", EditConditionHides, ClampMin="-1.0", ClampMax="1.0"))
	float Tracked_ReverseEngageDotThreshold = -0.5f;

	/** If a movement point is within this Distance threshold and below the Dot threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked", EditConditionHides, ClampMin="0"))
	float Tracked_ReverseEngageDistanceThreshold = 2500.0f;

	/** Max speed when reversing, differs from overall max speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked", EditConditionHides, ClampMin="0"))
	float Tracked_ReverseMaxSpeed = 300.0f;

	/** Curve representing forward throttle vs. direction misalignment for tracked vehicles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Tracked", EditConditionHides))
	FRuntimeFloatCurve Tracked_ThrottleVsMisalignmentDeg;

	// Wheeled Mode Properties
	// =================================================================================================================

	/** Maximum turn rate in degrees for wheeled vehicles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides))
	float Wheeled_MaxTurnRateDeg = 60.0f;

	/** If a movement point is below this Dot threshold and within the Distance threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="-1.0", ClampMax="1.0"))
	float Wheeled_ReverseEngageDotThreshold = -0.5f;

	/** If a movement point is within this Distance threshold and below the Dot threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="0"))
	float Wheeled_ReverseEngageDistanceThreshold = 2500.0f;

	/** Max speed when reversing, differs from overall max speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="0"))
	float Wheeled_ReverseMaxSpeed = 300.0f;

	/** Represents the distance between axels (cm), affects turn radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="1"))
	float Wheeled_Wheelbase = 220.0f;

	/** Maximum front wheel steering angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="1", ClampMax="60"))
	float Wheeled_MaxSteerAngleDeg = 60.0f;

	/** How fast we approach desired steer angle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Wheeled", EditConditionHides, ClampMin="0.1", ClampMax="10"))
	float Wheeled_SteerResponse = 3.0f;

	// Hover Mode Properties
	// =================================================================================================================

	/** Maximum turn rate in degrees for hover vehicles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Hover", EditConditionHides))
	float Hover_MaxTurnRateDeg = 60.0f;

	/** If a movement point is below this Dot threshold and within the Distance threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Hover", EditConditionHides, ClampMin="-1.0", ClampMax="1.0"))
	float Hover_ReverseEngageDotThreshold = -0.5f;

	/** If a movement point is within this Distance threshold and below the Dot threshold, 
	 * then this vehicle will reverse to that point rather than rotate and drive to it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Hover", EditConditionHides, ClampMin="0"))
	float Hover_ReverseEngageDistanceThreshold = 2500.0f;

	/** Max speed when reversing, differs from overall max speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement Configuration", 
		meta=(EditCondition="MovementMode==ESAFMovementMode::Hover", EditConditionHides, ClampMin="0"))
	float Hover_ReverseMaxSpeed = 300.0f;

protected:

	virtual void PostLoad() override;

};