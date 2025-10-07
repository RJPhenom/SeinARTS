#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "SAFCameraPawn.generated.h"

class UCameraComponent;
class USceneComponent;
class AActor;

/**
 * SAFCameraPawn
 *
 * A default camera pawn for RTS PoV control in the SeinARTS Framework.
 * Provides basic movement, rotation, and zooming functionality.
 * 
 * The SAFCameraPawn class handles default movement and control of the player
 * PoV in an RTS setting. You can use other camera plugins, there are several,
 * but you will need to wire them correctly to the SAFPlayerController yourself.
 * For quick setup and default wirings, a Blueprint has been provided in the 
 * plugin content folder for your use. The intention is this will be fully functional
 * as your game's camera system and would not require another plugin or substantial 
 * controll logic improvements, but you may need to expand, subclass, or use another 
 * solution based on your project's needs. Please check the SeinARTS Roadmap for
 * planned improvements to the default SAFCameraPawn
 */
UCLASS(ClassGroup=(SeinARTS), Blueprintable, BlueprintType, meta=(DisplayName="SeinARTS Camera Pawn"))
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFCameraPawn : public APawn {

	GENERATED_BODY()

public:

	ASAFCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

public:

  // ===========================================================================
	//                              Components
	// ===========================================================================

  // Pivot for yaw/pitch; parent of Camera.
  UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
  TObjectPtr<USceneComponent> Pivot;

  // Camera component.
  UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
  TObjectPtr<UCameraComponent> Camera;

  // ===========================================================================
	//                             Camera Control
	// ===========================================================================

  // Convenience to get current camera yaw.
  UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
  float GetCameraYaw() const { return GetActorRotation().Yaw; }

  // Convenience to get current camera pitch.
  UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
  float GetCameraPitch() const { return GetActorRotation().Pitch; }

  // Convenience to get current camera roll.
  UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
  float GetCameraRoll() const { return GetActorRotation().Roll; }

  // Optional actor to follow. If valid, pawn matches its world location each Tick.
  UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS|Camera")
  TObjectPtr<AActor> FollowTarget = nullptr;

  // Movement speed used by default (set at BeginPlay in BP).
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Speed")
  float BaseMoveSpeed = 10.f;

  // Movement speed when Shift is held (or when toggled via ToggleSpeed).
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Speed")
  float ShiftMoveSpeed = 50.f;

  // Current move speed (BP sets to BaseMoveSpeed on BeginPlay).
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="SeinARTS Camera|Speed")
  float MoveSpeed = 0.f;

  // Yaw/Pitch degrees per second for rotate input.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Speed")
  float RotationSpeed = 10.f;

  // Zoom units per second (or per input tick). Value must be negative or zooming will appear inversed.
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Speed")
  float ZoomSpeed = -250.f;

  // Min allowed camera distance/height (depending on your rig). Values must be negative (e.g. -300).
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Zoom")
  float MinZoom = -1000.f;

  // Max allowed camera distance/height. Vlaues must be negative (e.g. -5000).
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Zoom")
  float MaxZoom = -3000.f;

  // Default pitch angle applied by ResetCamera().
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Angles")
  float DefaultAngle = -30.f;

	// Minimum pitch angle. Vlaues should be negative, must be greater than MaxAngle (default -90 degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Angles")
	float MinAngle = -10.f;

	// Maximum pitch angle. Values should be negative, must be less than MinAngle (default -10 degrees).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS Camera|Angles")
	float MaxAngle = -90.f;

  // Sets the follow target safely
  UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
  void Follow(AActor* Actor);

	// Resets camera to default zoom and angle.
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void ResetCamera();

	// Toggles between BaseMoveSpeed and ShiftMoveSpeed.
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void ToggleSpeed(bool bFastMode) { MoveSpeed = bFastMode ? ShiftMoveSpeed : BaseMoveSpeed; }

	// Pans the camera.
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void PanCamera(FVector2D Input);

	// Rotates / Tilts the camera around the pivot.
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void RotateCamera(FVector2D Input);

	// Controls zooming in/out of the camera.
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void ZoomCamera(float AxisValue);

	// Converts a desired world location to a bounded location within map limits.
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	FVector GetBoundedLocation(const FVector& DesiredWorldLocation) const;
};
