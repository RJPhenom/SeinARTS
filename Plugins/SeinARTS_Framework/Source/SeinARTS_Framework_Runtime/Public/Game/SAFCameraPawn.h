

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInput/Public/EnhancedInputComponent.h"
#include "InputMappingContext.h"
#include "Camera/CameraComponent.h"
#include "SAFObject.h"
#include "SAFCameraPawn.generated.h"

// Dispatchers (these are useful for users who will be scripting events
// based on inputs called here).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraSnapDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraPanDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraRotateDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraZoomDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FToggleMapModeDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FToggleFollowModeDispatcher);

UCLASS()
class SEINARTS_FRAMEWORK_RUNTIME_API ASAFCameraPawn : public APawn
{
	GENERATED_BODY()

private:

    bool isFreeCam;

    // ===============================
    //      ENHANCED INPUT SYSTEM
    // ===============================

    FVector2D GetMouseDelta();

    // Action Bindings
    void PanCamera(const FInputActionValue& value);
    void RotateCamera(const FInputActionValue& value);
    void ZoomCamera(const FInputActionValue& value);

    void MapModeToggle(const FInputActionValue& value);
    void FollowModeToggle(const FInputActionValue& value);

    // Action Values
    FEnhancedInputActionValueBinding* MousePanBinding;
    FEnhancedInputActionValueBinding* MouseRotateBinding;
    FEnhancedInputActionValueBinding* FastPanBinding;

    FEnhancedInputActionValueBinding* HoldFollowModeBinding;


protected:

    virtual void BeginPlay() override;


public:

    ASAFCameraPawn();

    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;


    // ===============================
    //      ENHANCED INPUT SYSTEM
    // ===============================

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* MousePanAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* MouseRotateAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* MouseZoomAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* KeyPanAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* KeyRotateAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* KeyZoomAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* FastPanAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* ToggleMapModeAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* ToggleFollowModeAction;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Input")
    UInputAction* HoldFollowModeAction;



    // ===============================
    //        EVENT DISPATCHERS
    // ===============================

    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraSnapDispatcher CameraSnapDispatcher;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraPanDispatcher CameraPanDispatcher;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraRotateDispatcher CameraRotateDispatcher;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraZoomDispatcher CameraZoomDispatcher;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FToggleMapModeDispatcher ToggleMapModeDispatcher;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FToggleFollowModeDispatcher ToggleFollowModeDispatcher;


    // ===============================
    //           COMPONENTS
    // ===============================

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Components")
    USpringArmComponent* SpringArmComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Components")
    UCameraComponent* CameraComponent;


    // ===============================
    //    TOGGLES & MODE PROPERTIES
    // ===============================

    // If the camera is in Map Mode: Map Mode is a zoomed-out macro-view that is not
    // necessary to utilize. Returns if false if the camera is in Local Mode (default).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Toggles & Modes")
    bool MapMode = false;

    // If the camera is in Follow Mode: Follow Mode is when the camera follows an object
    // in the game world, keeping it centered. Requires FollowTarget to be set in order
    // to work.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Toggles & Modes")
    bool FollowMode = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Toggles & Modes")
    float MapModeOffset = 100.0f;

    // Toggles the FreeCam setting: FreeCam is for when the desired control scheme is for a
    // "floating head in space", otherwise the camera is the default "anchored pivot point"
    // Where the camera follows, rotates, and tilts relative to a position in view, usually
    // following a ground plane.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SeinARTS|Toggles & Modes")
    bool FreeCam = false;

    // Stored values for toggling back to LocalMode from MapMode.
    float StoredLocalZoom;
    FVector StoredLocalPos;

    // Reference to the object to follow if camera is in follow mode.
    TWeakObjectPtr<ASAFObject> FollowTarget;


    // ===============================
    //        CONTROL PROPERTIES
    // ===============================

    // The default incline or camera angle for your game. 0 = straight horizontal view. 90
    // = straight top-down view. For a 3D RTS a value below 45 is usually ideal. Default is 
    // 22.5f.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
    float DefaultAngle = 22.5f;


    // ===============================
    //        ZOOM PROPERTIES
    // ===============================
    
    // Controls the zoom speed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomSpeed = 100.0f;

    // Controls the zoom speed amplification when in MapMode
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomCoefficient = 10.0f;

    // Minimum zoom Level (no effect if FreeCam)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomMin = 10.0f;

    // Maximum zoom level (no effect if FreeCam)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomMax = 1000.0f;

    // Zoom is smoothed using linear interpolation. This controls the alpha input of the
    // interpolation. 
    //      0 = 100% current value (no zoom) 
    //    0.5 = 50% (default)
    //      1 = 100% target value (no smoothing)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomInterpFactor = 0.5f;


    // ===============================
    //       MOVEMENT PROPERTIES
    // ===============================

    // Controls the base camera movement speed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
    float MoveSpeed = 10.0f;

    // Controls movement speed amplification when in MapMode
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
    float MoveCoefficient = 10.0f;

    // Controls movement speed amplification when "shift-panning" or fast-scrolling.
    // To disable/not use fast scrolling, set to 1.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
    float MoveFastCoefficient = 2.0f;

    // Movement is smoothed using linear interpolation. This controls the alpha input of 
    // the interpolation. 
    //      0 = 100% current value (no panning) 
    //    0.5 = 50% (default)
    //      1 = 100% target value (no smoothing)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float MoveInterpFactor = 0.5f;


    // ===============================
    //       ROTATION PROPERTIES
    // ===============================

    // Controls rotation speed of the camera.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Rotation")
    float RotationSpeed = 1.0f;

    // Inverts the Y input. This effects pitch rotation, and is on by default.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Rotation")
    bool InvertYAxis = true;

    // Requires the mouse panning button (Default: MMB) to be pressed in order to
    // trigger mouse rotation. This action then blacks panning until the Activate
    // Mouse Rotation action (Default: Left Alt). Off by default.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Rotation")
    bool RequiresMousePanAction = false;


    // ===============================
    //       CONTROL FUNCTIONS
    // ===============================


    // Snaps the camera to a map-bounds safe vector based on the input vector.
    // Optionally snaps zoom and rotation as well.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement")
    void SnapCamera(FVector Vector, FVector LookAtVector, float Zoom);

    // Pans the camera along a plane: plane is the ground/anchor plane if
    // in default mode, or it is in the view plane if in FreeCam.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement")
    void PanCamera(float PanX, float PanY, bool Fast);

    // Pans the camera along the panning plane normal (useful for 3D RTS
    // games like Homeworld, or if in FreeCam).
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Movement")
    void PanCameraAltitude(float PanZ, bool Fast);

    // Rotates the camera (Roll rotation omitted as its not generally used, 
    // and this simplifies rotations controls greatly).
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Rotation")
    void RotateCamera(float Yaw, float Pitch);

    // Snaps camera rotation to look at the given vector. Optionally uses a 
    // rotator.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Rotation")
    void SnapCameraRotation(FVector LookAtVector, FRotator Rotator = FRotator::ZeroRotator);

    // Gets the zoom bounds, relative to MapMode.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Zoom")
    FVector2D GetCameraZoomBounds();

    // Checks if the given zoom value is within zoom max/min.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Zoom")
    bool CheckZoomWithinBounds(float Zoom);

    // Zooms the camera. Moves camera forward or backwards along view axis
    // if in FreeCam.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Zoom")
    void ZoomCamera(float Zoom);

    // Snaps the zoom level to the specified amount, if within bounds.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Zoom")
    void SnapCameraZoom(float Zoom);


    // ===============================
    //     TOGGLES & CAMERA MODES 
    // ===============================

    // Toggles the camera into map mode. See MapMode property for more info.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    void ToggleMapMode();

    // Toggles the camera into follow mode. See FollowMode property for more info.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    void ToggleFollowMode();

    // Returns if follow mode is active. Follow mode is active when either following OR 
    // HoldFollowModeBinding is true.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    bool GetFollowMode() const { return FollowMode || (bool)HoldFollowModeBinding; };

    // Returns the current follow target.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    ASAFObject* GetFollowTarget() const { return FollowTarget.Get(); };

    // Sets the object to follow in follow mode.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    void SetFollowTarget(ASAFObject* Target) { FollowTarget = Target; };

    // Clears the object to follow. This will also deactive following.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    void ClearFollowTarget() { FollowTarget = nullptr; FollowMode = false; };

    // Toggles default functionality for when map toggles.
    UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Toggles & Modes")
    void OnToggleMapMode();
    virtual void OnToggleMapMode_Implementation();

    // Toggles default functionality for when follow mode toggles.
    UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Toggles & Modes")
    void OnToggleFollowMode();
    virtual void OnToggleFollowMode_Implementation();
};
