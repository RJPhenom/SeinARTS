#include "Classes/SAFCameraPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Utils/SAFMathLibrary.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"
#include "Net/UnrealNetwork.h"

ASAFCameraPawn::ASAFCameraPawn() {
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	Pivot  = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Pivot;
	Camera->SetupAttachment(Pivot);
	UpdateInternalValues();
}

void ASAFCameraPawn::BeginPlay() {
	Super::BeginPlay();
	ResetCamera();
	SetScrollSpeed(BaseScrollSpeed);
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
/** Sets the follow target safely */
void ASAFCameraPawn::Follow(AActor* Actor) {
	if (SAFLibrary::IsActorPtrValidSeinARTSActor(Actor)) {
		SAFDEBUG_SUCCESS(FORMATSTR("Set new follow target '%s'!", *Actor->GetName()));
		FollowTarget = Actor;
	} else SAFDEBUG_WARNING("Follow failed: invalid actor.");
}

/** Resets camera to default zoom and angle. */
void ASAFCameraPawn::ResetCamera() {
	const FRotator NewRotation(DefaultAngle, 0.f, 0.f);
	const FRotator FinalRotation = NewRotation + StartTransform.GetRotation().Rotator();

	SAFDEBUG_INFO("Resetting camera...");
	SAFDEBUG_INFO(FORMATSTR("StartRot = %s", *StartTransform.GetRotation().Rotator().ToString()));
	SAFDEBUG_INFO(FORMATSTR("NewRotation = %s", *NewRotation.ToString()));
	SAFDEBUG_INFO(FORMATSTR("FinalRotation = %s", *FinalRotation.ToString()));

	SetActorRotation(FinalRotation);

	// Place the camera along its local X axis at MaxZoom
	if (Camera) Camera->SetRelativeLocation(FVector(MaxZoom, 0.f, 0.f));
}

/** Pans the camera. Disables follow mode, if set. */
void ASAFCameraPawn::PanCamera(FVector2D Input) {
	FollowTarget = nullptr;

	const float Yaw = Camera ? Camera->GetComponentRotation().Yaw : GetActorRotation().Yaw;
	const FRotator YawOnly(0.f, Yaw, 0.f);
	const FVector Right   = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);
	const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);

	if (!Input.IsNearlyZero()) {
		AddMovementInput(Right,   Input.X * ScrollSpeed, false);
		AddMovementInput(Forward, Input.Y * ScrollSpeed, false);
	}

	const FVector PendingLocation = ConsumeMovementInputVector();
	if (!PendingLocation.IsNearlyZero()) {
		const FVector Desired = GetActorLocation() + PendingLocation;
		const FVector Bounded = GetBoundedLocation(Desired);
		SetActorLocation(Bounded, false, nullptr, ETeleportType::None);
	}
}

/** Rotates / Tilts the camera around the pivot. */
void ASAFCameraPawn::RotateCamera(FVector2D Input) {
	const float CurPitch = GetCameraPitch();
	const float CurYaw   = GetCameraYaw();
	const float TargetPitch = CurPitch + (-Input.Y * RotationSpeed);
	const float TargetYaw   = CurYaw   + ( Input.X * RotationSpeed);
	const float NewPitch = SAFMathLibrary::ReverseSmoothClamp(CurPitch, TargetPitch, MinAngle, MaxAngle);
	const float NewYaw = FMath::Lerp(CurYaw, TargetYaw, 0.5f);
	SetActorRotation(FRotator(NewPitch, NewYaw, 0.f));
}

/** Controls zooming in/out of the camera. */
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

/** Converts a desired world location to a bounded location within map limits. */
FVector ASAFCameraPawn::GetBoundedLocation(const FVector& DesiredWorldLocation) const {
	// TODO
	return DesiredWorldLocation;
}

/** Update internal negative values from editor positive values */
void ASAFCameraPawn::UpdateInternalValues() {
	ZoomSpeed 		= -FMath::Abs(ZoomSpeedEditor);
	MinZoom 		= -FMath::Abs(MinZoomEditor);
	MaxZoom 		= -FMath::Abs(MaxZoomEditor);
	DefaultAngle 	= -FMath::Abs(DefaultAngleEditor);
	MinAngle 		= -FMath::Abs(MinAngleEditor);
	MaxAngle 		= -FMath::Abs(MaxAngleEditor);
}

/** Set zoom speed using positive value. */
void ASAFCameraPawn::SetZoomSpeed(float NewZoomSpeed) {
	ZoomSpeedEditor = FMath::Abs(NewZoomSpeed);
	ZoomSpeed = -ZoomSpeedEditor;
}

/** Set min zoom distance using positive value. */
void ASAFCameraPawn::SetMinZoom(float NewMinZoom) {
	MinZoomEditor = FMath::Abs(NewMinZoom);
	MinZoom = -MinZoomEditor;
}

/** Set max zoom distance using positive value. */
void ASAFCameraPawn::SetMaxZoom(float NewMaxZoom) {
	MaxZoomEditor = FMath::Abs(NewMaxZoom);
	MaxZoom = -MaxZoomEditor;
}

/** Set default angle using positive value (-90 to 90 degrees). */
void ASAFCameraPawn::SetDefaultAngle(float NewDefaultAngle) {
	DefaultAngleEditor = FMath::Clamp(NewDefaultAngle, MinAngle, MaxAngle);
	DefaultAngle = -DefaultAngleEditor;
}

/** Set min angle using positive value (-90 to 90 degrees). */
void ASAFCameraPawn::SetMinAngle(float NewMinAngle) {
	MinAngleEditor = FMath::Clamp(NewMinAngle, -90.f, 90.f);
	MinAngle = -MinAngleEditor;
}

/** Set max angle using positive value (-90 to 90 degrees). */
void ASAFCameraPawn::SetMaxAngle(float NewMaxAngle) {
	MaxAngleEditor = FMath::Clamp(NewMaxAngle, -90.f, 90.f);
	MaxAngle = -MaxAngleEditor;
}

// Replication
// ==================================================================================================
void ASAFCameraPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFCameraPawn, StartTransform);
}

#if WITH_EDITOR
void ASAFCameraPawn::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property) {
		const FName PropertyName = PropertyChangedEvent.Property->GetFName();
		
		// Update internal values when editor values change
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ASAFCameraPawn, ZoomSpeedEditor) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ASAFCameraPawn, MinZoomEditor) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ASAFCameraPawn, MaxZoomEditor) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ASAFCameraPawn, DefaultAngleEditor) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ASAFCameraPawn, MinAngleEditor) ||
			PropertyName == GET_MEMBER_NAME_CHECKED(ASAFCameraPawn, MaxAngleEditor)) {
			UpdateInternalValues();
		}
	}
}
#endif
