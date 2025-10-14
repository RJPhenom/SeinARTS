#pragma once

#include "CoreMinimal.h"
#include "Assets/SAFAsset.h"
#include "Engine/EngineTypes.h"
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Animation Blueprint class to drive the pawn's animations. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/** Transform offset for positioning the mesh relative to the pawn's collision capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Visual")
	FTransform MeshOffset = FTransform::Identity;

	// Capsule Configuration
	// =================================================================================================================
	/** Radius of the collision capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Capsule")
	float CapsuleRadius = 50.0f;

	/** Half-height of the collision capsule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Capsule")
	float CapsuleHalfHeight = 100.0f;

	// Cover Configuration
	// =================================================================================================================
	/** Whether this pawn can use cover mechanics. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cover")
	bool bUsesCover = true;

	// Movement Configuration
	// =================================================================================================================
	/** Movement component class to spawn (determines pawn behavior: infantry, vehicle, aircraft, etc.). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	TSubclassOf<USAFMovementComponent> MovementComponentClass;
	
	/** Maximum movement speed for this pawn (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	float MaxSpeed = 600.0f;

	/** Acceleration rate for movement (cm/s²). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	float Acceleration = 2048.0f;

	/** Deceleration rate when stopping (cm/s²). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	float Deceleration = 2048.0f;

	/** Maximum rotation rate (degrees per second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	float MaxRotationRate = 720.0f;

	/** Turning boost multiplier (0 = no turning boost, higher = more responsive turning). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	float TurningBoost = 0.0f;

	/** Whether to constrain movement to a plane (typically XY for RTS units). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement")
	bool bConstrainToPlane = true;

	/** If constraining to plane, this defines the plane normal (typically up vector for XY plane). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(EditCondition="bConstrainToPlane", EditConditionHides))
	FVector PlaneConstraintNormal = FVector::UpVector;

	/** Whether to snap to the constraint plane at spawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Movement", meta=(EditCondition="bConstrainToPlane", EditConditionHides))
	bool bSnapToPlaneAtStart = true;

protected:

	virtual void PostLoad() override;

};