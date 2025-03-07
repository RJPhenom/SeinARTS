

#pragma once

// Engine includes
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

// Framework includes
#include "SAFGameModeBase.h"
#include "SAFObject.h"

// Generated includes
#include "SAFCameraPawn.generated.h"

// Dispatchers (these are useful for users who will be scripting events
// based on inputs called here).
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraSnapDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraPanDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraRotateDispatcher);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCameraZoomDispatcher);

UCLASS()
class SEINARTS_FRAMEWORK_API ASAFCameraPawn : public APawn
{
	GENERATED_BODY()

private:

    bool isFreeCam;


protected:

    virtual void BeginPlay() override;


public:

    ASAFCameraPawn();

    virtual void Tick(float DeltaTime) override;
    virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

    // ===============================
    //        EVENT DISPATCHERS
    // ===============================

    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraSnapDispatcher OnCameraSnap;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraPanDispatcher OnCameraPan;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraRotateDispatcher OnCameraRotate;
    UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "SeinARTS|Event Dispatchers")
    FCameraZoomDispatcher OnCameraZoom;


    // ===============================
    //           COMPONENTS
    // ===============================

    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SeinARTS|Components")
    USpringArmComponent* SpringArmComponent;
    UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "SeinARTS|Components")
    UCameraComponent* CameraComponent;


    // ===============================
    //    TOGGLES & MODE PROPERTIES
    // ===============================

    // If the camera is in Map Mode: Map Mode is a zoomed-out macro-view that is not
    // necessary to utilize. Returns if false if the camera is in Local Mode (default).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Toggles & Modes")
    bool MapMode = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Toggles & Modes")
    float MapModeOffset;

    // Toggles the FreeCam setting: FreeCam is for when the desired control scheme is for a
    // "floating head in space", otherwise the camera is the default "anchored pivot point"
    // Where the camera follows, rotates, and tilts relative to a position in view, usually
    // following a ground plane.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Toggles & Modes")
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
    float ZoomSpeed;

    // Controls the zoom speed amplification when in MapMode
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomCoefficient;

    // Minimum zoom Level (no effect if FreeCam)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomMin;

    // Maximum zoom level (no effect if FreeCam)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Zoom")
    float ZoomMax;

    // ===============================
    //       MOVEMENT PROPERTIES
    // ===============================

    // Controls the base camera movement speed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
    float MoveSpeed;

    // Controls movement speed amplification when in MapMode
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
    float MoveCoefficient;

    // Controls movement speed amplification when "shift-panning" or fast-scrolling.
    // To disable/not use fast scrolling, set to 1.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Movement")
    float MoveFastCoefficient;


    // ===============================
    //       ROTATION PROPERTIES
    // ===============================

    // Controls rotation speed of the camera.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Rotation")
    float RotationSpeed;


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
    void RotateCamera(float Pitch, float Yaw);

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

    // Returns if there is a follow target, which indicates the camera is in
    // follow mode (if confused as to why, see ToggleFollow implementation).
    UFUNCTION(BlueprintPure, Category = "SeinARTS|Toggles & Modes")
    bool GetFollowMode() const { return FollowTarget.IsValid(); };

    // Returns the current follow target.
    UFUNCTION(BlueprintPure, Category = "SeinARTS|Toggles & Modes")
    ASAFObject* GetFollowTarget() const { return FollowTarget.Get(); };

    // Sets the object to follow in follow mode.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    void SetFollowTarget(ASAFObject* Target) { FollowTarget = Target; };

    // Clears the object to follow, thus deactivating follow mode.
    UFUNCTION(BlueprintCallable, Category = "SeinARTS|Toggles & Modes")
    void ClearFollowTarget() { FollowTarget = nullptr; };


};
