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

	// Components
	// =========================================================================================================
	/** Pivot for yaw/pitch; parent of Camera. */
	UPROPERTY(BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<USceneComponent> Pivot;

	/** Camera component. */
	UPROPERTY(BlueprintReadOnly, Category="Components", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCameraComponent> Camera;

	// Camera Control API
	// ===================================================================================================================================
	/** Convenience to get current camera yaw. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetCameraYaw() const { return GetActorRotation().Yaw; }

	/** Convenience to get current camera pitch. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetCameraPitch() const { return GetActorRotation().Pitch; }

	/** Convenience to get current camera roll. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetCameraRoll() const { return GetActorRotation().Roll; }

	/** Optional actor to follow. If valid, pawn matches its world location each Tick. */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="SeinARTS")
	TObjectPtr<AActor> FollowTarget = nullptr;

	/** Movement speed used by default (set at BeginPlay in BP). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS", meta=(ClampMin="0", DisplayName="Scroll Speed"))
	float BaseScrollSpeed = 10.f;

	/** Movement speed when Shift is held (or when toggled via ToggleSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS", meta=(ClampMin="0", DisplayName="Shift Scroll Speed"))
	float ShiftScrollSpeed = 50.f;

	/** Yaw/Pitch degrees per second for rotate input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS", meta=(ClampMin="0", DisplayName="Rotation Speed"))
	float RotationSpeed = 10.f;

	/** Zoom units per second (or per input tick). Enter as positive value. */
	UPROPERTY(EditAnywhere, Category="SeinARTS", meta=(ClampMin="0", DisplayName="Zoom Speed"))
	float ZoomSpeedEditor = 250.f;

	/** Min allowed camera distance/height. Enter as positive value. */
	UPROPERTY(EditAnywhere, Category="SeinARTS", meta=(ClampMin="0", DisplayName="Min Zoom Distance"))
	float MinZoomEditor = 1000.f;

	/** Max allowed camera distance/height. Enter as positive value. */
	UPROPERTY(EditAnywhere, Category="SeinARTS", meta=(ClampMin="0", DisplayName="Max Zoom Distance"))
	float MaxZoomEditor = 3000.f;

	/** Default pitch angle. Enter as positive value (-90 to 90 degrees). 
	 * 
	 * NOTE: This is additive with the PlayerStart's rotation that this camera pawn spawns at. 
	 * To override completely with the PlayerStart's rotation, simply set the DefaultAngle to 0. */
	UPROPERTY(EditAnywhere, Category="SeinARTS", meta=(ClampMin="-90", ClampMax="90", DisplayName="Default Camera Angle"))
	float DefaultAngleEditor = 30.f;

	/** Minimum pitch angle. Enter as positive value (-90 to 90 degrees). */
	UPROPERTY(EditAnywhere, Category="SeinARTS", meta=(ClampMin="-90", ClampMax="90", DisplayName="Min Camera Angle"))
	float MinAngleEditor = 10.f;

	/** Maximum pitch angle. Enter as positive value (-90 to 90 degrees). */
	UPROPERTY(EditAnywhere, Category="SeinARTS", meta=(ClampMin="-90", ClampMax="90", DisplayName="Max Camera Angle"))
	float MaxAngleEditor = 90.f;

	// Blueprint Getters/Setters (User-Friendly Positive Values)
	// ===================================================================================================================================
	/** Get scroll speed as positive value. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetScrollSpeed() const { return ScrollSpeed; }

	/** Set scroll speed using positive value. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void SetScrollSpeed(float NewScrollSpeed) { ScrollSpeed = NewScrollSpeed; }

	/** Get zoom speed as positive value. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetZoomSpeed() const { return ZoomSpeedEditor; }

	/** Set zoom speed using positive value. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void SetZoomSpeed(float NewZoomSpeed);

	/** Get min zoom distance as positive value. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetMinZoom() const { return MinZoomEditor; }

	/** Set min zoom distance using positive value. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void SetMinZoom(float NewMinZoom);

	/** Get max zoom distance as positive value. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetMaxZoom() const { return MaxZoomEditor; }

	/** Set max zoom distance using positive value. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void SetMaxZoom(float NewMaxZoom);

	/** Get default angle as positive value (0-90 degrees). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetDefaultAngle() const { return DefaultAngleEditor; }

	/** Set default angle using positive value (0-90 degrees). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void SetDefaultAngle(float NewDefaultAngle);

	/** Get min angle as positive value (0-90 degrees). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetMinAngle() const { return MinAngleEditor; }

	/** Set min angle using positive value (0-90 degrees). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void SetMinAngle(float NewMinAngle);

	/** Get max angle as positive value (0-90 degrees). */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	float GetMaxAngle() const { return MaxAngleEditor; }

	/** Set max angle using positive value (0-90 degrees). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void SetMaxAngle(float NewMaxAngle);

	/** Sets the follow target safely */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void Follow(AActor* Actor);

	/** Resets camera to default zoom and angle. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void ResetCamera();

	/** Toggles between BaseScrollSpeed and ShiftScrollSpeed. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void ToggleSpeed(bool bFastMode) { ScrollSpeed = bFastMode ? ShiftScrollSpeed : BaseScrollSpeed; }

	/** Pans the camera. Disables follow mode, if set. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void PanCamera(FVector2D Input);

	/** Rotates / Tilts the camera around the pivot. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void RotateCamera(FVector2D Input);

	/** Controls zooming in/out of the camera. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Camera")
	void ZoomCamera(float AxisValue);

	/** Converts a desired world location to a bounded location within map limits. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Camera")
	FVector GetBoundedLocation(const FVector& DesiredWorldLocation) const;
	
	/** Start transform tracker. Used to properly align the camera start with the player start. 
	 * 
	 * NOTE: This is visible anywhere for debugging, but is set once and only once by the SAFGameMode 
	 * during player RestartPlayerAtPlayerStart().  Do not modify this value elsewhere. */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Replicated, Category="SeinARTS")
	FTransform StartTransform = FTransform::Identity;

protected:

	// Internal Values (Always Negative/Correct for Logic)
	// =================================================================================================
	/** Update internal negative values from editor positive values */
	void UpdateInternalValues();
	float ScrollSpeed = 0.f;
	float ZoomSpeed = -250.f;
	float MinZoom = -1000.f;
	float MaxZoom = -3000.f;
	float DefaultAngle = -30.f;
	float MinAngle = -10.f;
	float MaxAngle = -90.f;

	// Replication
	// =================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	#if WITH_EDITOR
	/** Called when a property is changed in the editor */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	#endif
};
