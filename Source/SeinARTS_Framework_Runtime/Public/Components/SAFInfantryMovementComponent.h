#pragma once

#include "CoreMinimal.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/EngineTypes.h"
#include "SAFInfantryMovementComponent.generated.h"

/**
 * USAFInfantryMovementComponent
 *
 * Lightweight infantry-flavored movement that mimics CMC feel
 * while staying on UFloatingPawnMovement for RTS use.
 */
UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFInfantryMovementComponent : public UFloatingPawnMovement {
	GENERATED_BODY()

public:

	USAFInfantryMovementComponent();

	// Move
	// =================================================================================================
	/** Max ground speed (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement", meta=(ClampMin="0"))
	float MaxGroundSpeed = 600.f;

	/** Braking friction factor (CMC-like). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement", meta=(ClampMin="0"))
	float BrakingFriction = 2.0f;

	/** Lock movement to XY plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement")
	bool bLockToXY = true;

	// Strafe / Facing
	// =================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement")
	bool bAllowStrafe = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement")
	bool bUseDesiredFacing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement")
	float DesiredFacingYaw = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement", meta=(ClampMin="1"))
	float AimRotationRateDeg = 720.f;

	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void SetDesiredFacingYaw(float InYaw) { DesiredFacingYaw = InYaw; bUseDesiredFacing = true; }

	UFUNCTION(BlueprintCallable, Category="SeinARTS|Infantry Movement")
	void ClearDesiredFacing() { bUseDesiredFacing = false; }

	// Separation
	// =================================================================================================
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement")
	bool bEnableSeparation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement", meta=(ClampMin="0"))
	float SeparationRadius = 120.f;

	/** Separation acceleration scale (cm/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement", meta=(ClampMin="0"))
	float SeparationStrength = 1500.f;

	/** Max separation velocity delta per tick (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement", meta=(ClampMin="0"))
	float SeparationMaxPush = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Infantry Movement")
	TEnumAsByte<ECollisionChannel> SeparationChannel = ECC_Pawn;

protected:

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Helpers
	// ==============================================================================
	FVector ConsumeWorldInput();
	FVector ComputeAcceleration(const FVector& InputWorld, float DeltaTime) const;
	void ApplySeparation(FVector& OutAccel, float DeltaTime);
	void ApplyRotation(float DeltaTime);
};
