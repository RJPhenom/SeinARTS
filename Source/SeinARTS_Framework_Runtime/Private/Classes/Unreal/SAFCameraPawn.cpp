#include "Classes/Unreal/SAFCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Utils/SAFMathLibrary.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"

ASAFCameraPawn::ASAFCameraPawn() {
	PrimaryActorTick.bCanEverTick = true;

	// Create default components
	Pivot  = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));

	// Build hierarchy
	RootComponent = Pivot;
	Camera->SetupAttachment(Pivot);
}

void ASAFCameraPawn::BeginPlay() {
	Super::BeginPlay();
	ResetCamera();
	MoveSpeed = BaseMoveSpeed;
}

void ASAFCameraPawn::Tick(float DeltaSeconds) {
	Super::Tick(DeltaSeconds);
	if (FollowTarget) {
		const FVector TargetLocation = FollowTarget->GetActorLocation();
		const FVector BoundedLocation = GetBoundedLocation(TargetLocation);
		SetActorLocation(BoundedLocation);
	}
}

// Camera Controls
// =================================================================================================================================
// Sets the follow target safely
void ASAFCameraPawn::Follow(AActor* Actor) {
	if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) {
		SAFDEBUG_SUCCESS(FORMATSTR("Set new follow target '%s'!", *Actor->GetName()));
		FollowTarget = Actor;
	} else SAFDEBUG_WARNING("Follow failed: invalid actor.");
}

// Reset actor rotation and zoom: 
//  pitch = DefaultAngle, yaw = 0, roll = 0 and
//  camera location = (MaxZoom, 0, 0) relative to Pivot.
void ASAFCameraPawn::ResetCamera() {
	const FRotator NewRotation(DefaultAngle, 0.f, 0.f);
	SetActorRotation(NewRotation);

	// Place the camera along its local X axis at MaxZoom
	if (Camera) {
		Camera->SetRelativeLocation(FVector(MaxZoom, 0.f, 0.f));
	}
}

// Pans the camera based on Input vector (X=right, Y=forward)
// Sets FollowTarget to nullptr to disable following, then gets the lateral movement using 
// Camera yaw (ignoring pitch/roll) and adds movement input. Input is immediately consumed 
// with bounding applied, so no need to call this every Tick.
void ASAFCameraPawn::PanCamera(FVector2D Input) {
	FollowTarget = nullptr;

	const float Yaw = Camera ? Camera->GetComponentRotation().Yaw : GetActorRotation().Yaw;
	const FRotator YawOnly(0.f, Yaw, 0.f);
	const FVector Right   = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
	const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);

	if (!Input.IsNearlyZero()) {
		AddMovementInput(Right,   Input.X * MoveSpeed, false);
		AddMovementInput(Forward, Input.Y * MoveSpeed, false);
	}

	const FVector PendingLocation = ConsumeMovementInputVector();
	if (!PendingLocation.IsNearlyZero()) {
		const FVector Desired = GetActorLocation() + PendingLocation;
		const FVector Bounded = GetBoundedLocation(Desired);
		SetActorLocation(Bounded, false, nullptr, ETeleportType::None);
	}
}

// Rotates / Tilts the camera based on Input vector (X=yaw, Y=pitch) with interpolation.
// Rotation is immediately set, so no need to call this every Tick.
void ASAFCameraPawn::RotateCamera(FVector2D Input) {
	const float CurPitch = GetCameraPitch();
	const float CurYaw   = GetCameraYaw();
	const float TargetPitch = CurPitch + (-Input.Y * RotationSpeed);
	const float TargetYaw   = CurYaw   + ( Input.X * RotationSpeed);
	const float NewPitch = SAFMathLibrary::ReverseSmoothClamp(CurPitch, TargetPitch, MinAngle, MaxAngle);
	const float NewYaw = FMath::Lerp(CurYaw, TargetYaw, 0.5f);
	SetActorRotation(FRotator(NewPitch, NewYaw, 0.f));
}

// Controls zooming in/out of the camera based on AxisValue input with interpolation.
// Zoom is immediately set, so no need to call this every Tick.
void ASAFCameraPawn::ZoomCamera(float AxisValue) {
	if (!Camera) {
		SAFDEBUG_ERROR("ZoomCamera failed: Camera component is nullptr.");
		return;
	}

	const FVector CurrRelativeLocation = Camera->GetRelativeLocation();
	const float DesiredX = CurrRelativeLocation.X + (-AxisValue * ZoomSpeed);
	const float ClampedX = SAFMathLibrary::ReverseSmoothClamp(CurrRelativeLocation.X, DesiredX, MinZoom, MaxZoom);
	const float NewX = FMath::Lerp(CurrRelativeLocation.X, ClampedX, 0.5f);
	Camera->SetRelativeLocation(FVector(NewX, CurrRelativeLocation.Y, CurrRelativeLocation.Z), false, nullptr, ETeleportType::None);
}

// Converts a desired world location to a bounded location within map limits.
FVector ASAFCameraPawn::GetBoundedLocation(const FVector& DesiredWorldLocation) const {
	// TODO
	return DesiredWorldLocation;
}
