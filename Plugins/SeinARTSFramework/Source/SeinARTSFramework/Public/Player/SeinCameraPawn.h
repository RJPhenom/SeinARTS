/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCameraPawn.h
 * @brief   RTS camera pawn with pan, rotate, zoom, edge-scroll,
 *          follow-target, and configurable world bounds.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Core/SeinEntityHandle.h"
#include "Simulation/SeinSnapshotCameraProvider.h"
#include "SeinCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UFloatingPawnMovement;
struct FInputActionValue;

/**
 * Default RTS camera pawn.
 * Uses a pivot (root) + spring arm + camera component pattern.
 * The pivot moves on the XY plane; the spring arm provides zoom and pitch.
 *
 * All tuning properties are exposed for Blueprint subclasses and per-instance overrides.
 *
 * Implements ISeinSnapshotCameraProvider so save-game snapshot capture/restore
 * round-trips this pawn's camera state. Custom camera pawns (e.g. third-party
 * marketplace assets) opt in by implementing the same interface — the framework
 * isn't hard-coupled to this specific class.
 */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinCameraPawn : public APawn, public ISeinSnapshotCameraProvider
{
	GENERATED_BODY()

public:
	ASeinCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ========== Components ==========

	/** Root pivot scene component — moves on the ground plane. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Camera")
	TObjectPtr<USceneComponent> CameraPivot;

	/** Spring arm controlling zoom distance and pitch angle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	/** Camera component attached to the spring arm. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Camera")
	TObjectPtr<UCameraComponent> CameraComponent;

	// ========== Pan Settings ==========

	/** Pan speed in units per second (WASD / edge scroll). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pan")
	float PanSpeed = 2000.0f;

	/** Enable screen-edge scrolling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pan")
	bool bEnableEdgeScroll = true;

	/** Screen edge margin in pixels that triggers edge scroll. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pan", meta = (EditCondition = "bEnableEdgeScroll", ClampMin = "1"))
	float EdgeScrollMargin = 20.0f;

	/** Edge scroll speed multiplier relative to PanSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pan", meta = (EditCondition = "bEnableEdgeScroll", ClampMin = "0.0"))
	float EdgeScrollSpeedMultiplier = 1.0f;

	// ========== Rotation Settings ==========

	/** Rotation speed in degrees per second (middle-mouse drag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Rotation")
	float RotationSpeed = 90.0f;

	// ========== Zoom Settings ==========

	/** Minimum zoom distance (closest to ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Zoom", meta = (ClampMin = "100.0"))
	float ZoomMin = 500.0f;

	/** Maximum zoom distance (farthest from ground). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Zoom", meta = (ClampMin = "100.0"))
	float ZoomMax = 5000.0f;

	/** Zoom step per scroll wheel tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Zoom")
	float ZoomStep = 200.0f;

	/** Zoom interpolation speed (higher = snappier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Zoom", meta = (ClampMin = "1.0"))
	float ZoomInterpSpeed = 10.0f;

	/** Default zoom distance at game start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Zoom")
	float DefaultZoomDistance = 2000.0f;

	// ========== Pitch Settings ==========

	/** Default spring arm pitch angle in degrees (negative = looking down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pitch")
	float CameraPitch = -50.0f;

	/** Minimum pitch angle (most top-down, e.g., -85). Must be negative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pitch", meta = (ClampMin = "-89.0", ClampMax = "-5.0"))
	float PitchMin = -85.0f;

	/** Maximum pitch angle (most flat/horizontal, e.g., -5). Must be negative. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pitch", meta = (ClampMin = "-89.0", ClampMax = "-5.0"))
	float PitchMax = -5.0f;

	/** Pitch sensitivity for orbit input (degrees per pixel of mouse movement). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Pitch")
	float OrbitPitchSensitivity = 0.3f;

	// ========== Bounds ==========

	/** Enable camera movement bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Bounds")
	bool bEnableBounds = false;

	/** World-space bounding box for camera pivot. Only XY is enforced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Camera|Bounds", meta = (EditCondition = "bEnableBounds"))
	FBox WorldBounds = FBox(FVector(-10000, -10000, -10000), FVector(10000, 10000, 10000));

	// ========== Follow Target ==========

	/** Follow an entity. Set to Invalid to stop following. Auto-disables on manual input. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Camera")
	void FollowEntity(FSeinEntityHandle Entity);

	/** Stop following any entity. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Camera")
	void StopFollowing();

	/** Whether the camera is currently following an entity. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Camera")
	bool IsFollowing() const { return FollowTarget.IsValid(); }

	/** Reset camera rotation to north-facing (yaw = 0). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Camera")
	void ResetRotation();

	/** Handle MMB pan input (direct mouse delta, moves pivot). Grab-the-ground style (inverted). */
	void HandleMMBPanInput(const FVector2D& MouseDelta);

	/** Handle Alt+MMB orbit input. X = yaw rotation, Y = pitch tilt (clamped). */
	void HandleOrbitInput(const FVector2D& MouseDelta);

	// ========== Input Handlers ==========

	/** Called by the player controller when camera pan input is received. */
	void HandlePanInput(const FVector2D& AxisValue);

	/** Called by the player controller when camera rotate input is received (keyboard Q/E). */
	void HandleRotateInput(float YawDelta);

	/** Called by the player controller when camera zoom input is received. */
	void HandleZoomInput(float ZoomDelta);

	// ========== Accessors ==========

	/** Get the current camera pivot world location. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Camera")
	FVector GetPivotLocation() const;

	/** Get the current camera yaw in degrees. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Camera")
	float GetCameraYaw() const;

	/** Get the current zoom distance. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Camera")
	float GetCurrentZoomDistance() const;

	/** Get the current camera pitch in degrees. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Camera")
	float GetCameraPitch() const;

	/**
	 * Snap the camera to a saved state (used by snapshot restore + future
	 * load-game flows). Sets pivot location, yaw, pitch, and target zoom in
	 * one call so the camera lands exactly where it was at capture time.
	 *
	 * Skips the smooth-interpolation logic that normal input drives — the
	 * jump should be instantaneous on restore (otherwise the camera lerps
	 * from current state, which feels wrong for a save-load).
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Camera")
	void SetCameraState(FVector PivotLocation, float Yaw, float Pitch, float ZoomDistance);

	// ========== ISeinSnapshotCameraProvider ==========

	virtual void CaptureCameraState_Implementation(FSeinCameraSnapshotData& OutData) override;
	virtual void RestoreCameraState_Implementation(const FSeinCameraSnapshotData& Data) override;

protected:
	/** Current target zoom distance (interpolated toward each tick). */
	float TargetZoomDistance = 2000.0f;

	/** Current pitch angle (dynamically modified by orbit input). */
	float CurrentPitch = -50.0f;

	/** Entity being followed (invalid = not following). */
	FSeinEntityHandle FollowTarget;

	/** Accumulated pan input this frame (from WASD + edge scroll). */
	FVector2D PendingPanInput = FVector2D::ZeroVector;

	/** Apply edge-scroll based on mouse screen position. */
	void UpdateEdgeScroll(float DeltaSeconds);

	/** Clamp pivot position to world bounds if enabled. */
	void ClampToBounds();

	/** Update follow-target tracking. */
	void UpdateFollowTarget(float DeltaSeconds);
};
