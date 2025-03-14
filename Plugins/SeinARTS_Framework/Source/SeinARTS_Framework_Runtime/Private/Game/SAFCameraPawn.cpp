


#include "Game/SAFCameraPawn.h"
#include "Kismet/KismetMathLibrary.h"
#include "SAFGameModeBase.h"
#include "SAFPlayerController.h"

ASAFCameraPawn::ASAFCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Component setup
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmComponent"));
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));

	RootComponent = SpringArmComponent;
	CameraComponent->SetupAttachment(SpringArmComponent);

	SpringArmComponent->bUsePawnControlRotation = true;
	SpringArmComponent->TargetArmLength = ZoomMax;

	// Event bindings
	ToggleMapModeDispatcher.AddDynamic(this, &ASAFCameraPawn::OnToggleMapMode);
}

void ASAFCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	SetCameraType(CameraType);
	AddControllerPitchInput(DefaultAngle);
}

void ASAFCameraPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Following is handle in tick because its inexpensive.
	if (GetFollowMode() && FollowTarget.IsValid()) SetActorLocation(FollowTarget->GetActorLocation());
}


// ===============================
//			  CONTROLS
// ===============================

void ASAFCameraPawn::SetCameraType(TEnumAsByte<SAFEnumerator_CameraPawnType> Type) {
	camType = Type.GetValue();

	// Setup for each type of camera
	if (camType == SAFEnumerator_CameraPawnType::PivotPoint) {
		SpringArmComponent->TargetArmLength = ZoomMax;
	}

	else if (camType == SAFEnumerator_CameraPawnType::FloatingHead) {
		SpringArmComponent->TargetArmLength = 0.0f;
	}
}

void ASAFCameraPawn::ToggleMapMode() {
	MapMode = !MapMode;
	ToggleMapModeDispatcher.Broadcast();
}

// Default response to the above broadcast
void ASAFCameraPawn::OnToggleMapMode_Implementation() {

	switch (camType) {
	case SAFEnumerator_CameraPawnType::PivotPoint:
		if (MapMode) {
			StoredLocalZoom = SpringArmComponent->TargetArmLength;
			SpringArmComponent->TargetArmLength = MapModeOffset;
		}

		else {
			SpringArmComponent->TargetArmLength = StoredLocalZoom;
		}

		break;

	case SAFEnumerator_CameraPawnType::FloatingHead:
		ZoomCamera(MapMode ? MapModeOffset * -1 : MapModeOffset);
		break;

	default:
		UE_LOG(LogTemp, Error, TEXT("Unknown camera type."));
		return;

	}
}

void ASAFCameraPawn::ToggleFollowMode() {
	FollowMode = !FollowMode;
	ToggleFollowModeDispatcher.Broadcast();
}

// Default response to the above broadcast
void ASAFCameraPawn::OnToggleFollowMode_Implementation() {
	if (!FollowMode) {
		ASAFPlayerController* controller = Cast<ASAFPlayerController>(GetController());

		if (controller) {
			SetFollowTarget(controller->GetSelected());
		}

		else {
			UE_LOG(LogTemp, Error, TEXT("Follow Mode Toggle failed: player controller is incorrect type! (Requires SAFPlayerController)"));
			return;
		}
	}

	FollowMode = !FollowMode;
}


// ===============================
//         ENHANCED INPUT
// ===============================

FVector2D ASAFCameraPawn::GetMouseDelta() {
	FVector2D MouseDelta = FVector2D::ZeroVector;

	if (APlayerController* controller = GetController<APlayerController>()) {
		controller->GetInputMouseDelta(MouseDelta.X, MouseDelta.Y);
	}

	return MouseDelta;
}

void ASAFCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent)) {

		// Action Bindings
		EnhancedInputComponent->BindAction(MousePanAction, ETriggerEvent::Triggered, this, &ASAFCameraPawn::PanCamera);
		EnhancedInputComponent->BindAction(MouseRotateAction, ETriggerEvent::Triggered, this, &ASAFCameraPawn::RotateCamera);
		EnhancedInputComponent->BindAction(MouseZoomAction, ETriggerEvent::Triggered, this, &ASAFCameraPawn::ZoomCamera);
		EnhancedInputComponent->BindAction(KeyPanAction, ETriggerEvent::Triggered, this, &ASAFCameraPawn::PanCamera);
		EnhancedInputComponent->BindAction(KeyRotateAction, ETriggerEvent::Triggered, this, &ASAFCameraPawn::RotateCamera);
		EnhancedInputComponent->BindAction(KeyZoomAction, ETriggerEvent::Triggered, this, &ASAFCameraPawn::ZoomCamera);

		EnhancedInputComponent->BindAction(ToggleMapModeAction, ETriggerEvent::Completed, this, &ASAFCameraPawn::MapModeToggle);
		EnhancedInputComponent->BindAction(ToggleFollowModeAction, ETriggerEvent::Completed, this, &ASAFCameraPawn::FollowModeToggle);

		// Action Values
		MousePanBinding = &EnhancedInputComponent->BindActionValue(MousePanAction);
		MouseRotateBinding = &EnhancedInputComponent->BindActionValue(MouseRotateAction);
		AltitudePanBinding = &EnhancedInputComponent->BindActionValue(AltitudePanAction);
		FastPanBinding = &EnhancedInputComponent->BindActionValue(FastPanAction);

		HoldFollowModeBinding = &EnhancedInputComponent->BindActionValue(HoldFollowModeAction);
	}
}

void ASAFCameraPawn::MapModeToggle(const FInputActionValue& Value) {
	ToggleMapMode();
}

void ASAFCameraPawn::FollowModeToggle(const FInputActionValue& Value) {
	ToggleFollowMode();
}


// ===============================
//       MOVEMENT CONTROLS
// ===============================

void ASAFCameraPawn::SnapCamera(FVector Vector, FVector LookAtVector, float Zoom) {
	FVector safeVector = Cast<ASAFGameModeBase>(GetWorld()->GetAuthGameMode())->GetSafeVectorWithinMapBounds(Vector);
	
	SetActorLocation(safeVector);
	if (LookAtVector != FVector::ZeroVector) SnapCameraRotation(LookAtVector);
	if (Zoom != 0.0f) SnapCameraZoom(Zoom);

	CameraSnapDispatcher.Broadcast();
}


void ASAFCameraPawn::PanCamera(const FInputActionValue& Value) {
	FVector2D panValue = (Value.GetValueType() == EInputActionValueType::Axis2D) ? Value.Get<FVector2D>() : GetMouseDelta();
	bool fastPan = FastPanBinding->GetValue().Get<bool>();
	bool altitudePan = AltitudePanBinding->GetValue().Get<bool>();

	if (altitudePan) {
		PanCameraAltitude(panValue.Y, fastPan);
	}
	
	else if (!RequiresMousePanAction || !MouseRotateBinding->GetValue().Get<bool>()) {
		PanCamera(panValue.X, panValue.Y, fastPan);
	}
}

void ASAFCameraPawn::PanCamera(float PanX, float PanY, bool Fast) {
	ClearFollowTarget();

	// Build movement input
	float scaleX = PanX * MoveSpeed;
	float scaleY = PanY * MoveSpeed;

	if (MapMode) {
		scaleX *= MoveCoefficient;
		scaleY *= MoveCoefficient;
	}

	if (Fast) {
		scaleX *= MoveFastCoefficient;
		scaleY *= MoveFastCoefficient;
	}

	FRotator controlRot = GetControlRotation();
	FVector rightVector = UKismetMathLibrary::GetRightVector(controlRot);
	FVector fwdVector = UKismetMathLibrary::Cross_VectorVector(rightVector, FVector::UpVector);

	AddMovementInput(rightVector, scaleX);
	AddMovementInput(fwdVector, scaleY);

	// Consume and wrap in bounds, then move
	FVector movementVector = ConsumeMovementInputVector() + GetActorLocation();
	FVector safeMovementVector = Cast<ASAFGameModeBase>(GetWorld()->GetAuthGameMode())->GetSafeVectorWithinMapBounds(movementVector);

	FVector newLocation = FMath::Lerp(GetActorLocation(), safeMovementVector, MoveInterpFactor);

	SetActorLocation(newLocation);
}

void ASAFCameraPawn::PanCameraAltitude(float PanZ, bool Fast) {
	ClearFollowTarget();

	// Build input
	float scaleZ = PanZ * MoveSpeed;

	if (MapMode) scaleZ *= MoveCoefficient;
	if (Fast) scaleZ *= MoveFastCoefficient;

	AddMovementInput(FVector::UpVector, scaleZ);

	// Consume and wrap in bounds
	FVector movementVector = ConsumeMovementInputVector() + GetActorLocation();
	FVector safeMovementVector = Cast<ASAFGameModeBase>(GetWorld()->GetAuthGameMode())->GetSafeVectorWithinMapBounds(movementVector);

	SetActorLocation(safeMovementVector);
}


// ===============================
//       ROTATION CONTROLS
// ===============================

void ASAFCameraPawn::SnapCameraRotation(FVector LookAtVector, FRotator Rotator) {
	if (Rotator != FRotator::ZeroRotator) {
		GetController()->SetControlRotation(Rotator);
	}

	else {
		FRotator lookRot = UKismetMathLibrary::FindLookAtRotation(GetPawnViewLocation(), LookAtVector);
		GetController()->SetControlRotation(lookRot);
	}

	CameraSnapDispatcher.Broadcast();
}

void ASAFCameraPawn::RotateCamera(const FInputActionValue& Value) {
	FVector2D rotValue = (Value.GetValueType() == EInputActionValueType::Axis2D) ? Value.Get<FVector2D>() : GetMouseDelta();

	if (!RequiresMousePanAction || MousePanBinding->GetValue().Get<bool>()) {
		RotateCamera(rotValue.X, rotValue.Y);
	}
}

void ASAFCameraPawn::RotateCamera(float Yaw, float Pitch) {
	float scaleYaw = Yaw * RotationSpeed;
	float scalePitch = Pitch * RotationSpeed * (InvertYAxis) ? -Pitch : Pitch;

	AddControllerYawInput(scaleYaw);
	AddControllerPitchInput(scalePitch);
}


// ===============================
//          ZOOM CONTROLS
// ===============================

FVector2d ASAFCameraPawn::GetCameraZoomBounds() {
	float minZoom = (MapMode) ? ZoomMin * ZoomCoefficient : ZoomMin;
	float maxZoom = (MapMode) ? ZoomMax * ZoomCoefficient : ZoomMax;

	return FVector2D(minZoom, maxZoom);
}

bool ASAFCameraPawn::CheckZoomWithinBounds(float Zoom) {
	FVector2D zoomBounds = GetCameraZoomBounds();

	return zoomBounds.X < Zoom && Zoom < zoomBounds.Y;
}

void ASAFCameraPawn::SnapCameraZoom(float Zoom) {
	if (CheckZoomWithinBounds(Zoom)) SpringArmComponent->TargetArmLength = Zoom;

	CameraSnapDispatcher.Broadcast();
}

void ASAFCameraPawn::ZoomCamera(const FInputActionValue& Value) {
	float zoomValue = Value.Get<float>();

	if (Value.GetValueType() != EInputActionValueType::Axis1D) {
		UE_LOG(LogTemp, Error, TEXT("ZoomCamera action aborted: Expected Axis1D but got %s"), *UEnum::GetValueAsString((Value.GetValueType())));
		return;
	}

	ZoomCamera(zoomValue);
}

void ASAFCameraPawn::ZoomCamera(float Zoom) {
	float scaleZoom = Zoom * ZoomSpeed;
	if (MapMode) scaleZoom *= ZoomCoefficient;

	switch (camType) {
		case SAFEnumerator_CameraPawnType::PivotPoint : {
			scaleZoom *= -1; // Inverted because we subtract from springarm length as we 'zoom in'

			float zoomCurrent = SpringArmComponent->TargetArmLength;
			float zoomTarget = scaleZoom + zoomCurrent;

			if (CheckZoomWithinBounds(zoomTarget)) {
				float newZoom = FMath::Lerp(zoomCurrent, zoomTarget, ZoomInterpFactor);
				SpringArmComponent->TargetArmLength = newZoom;
			}
			break;
		}

		case SAFEnumerator_CameraPawnType::FloatingHead: {
			FRotator controlRot = GetControlRotation();
			FVector ZoomVector = UKismetMathLibrary::GetForwardVector(controlRot);

			AddMovementInput(ZoomVector, scaleZoom);

			// Consume and wrap in bounds, then move
			FVector movementVector = ConsumeMovementInputVector() + GetActorLocation();
			FVector safeMovementVector = Cast<ASAFGameModeBase>(GetWorld()->GetAuthGameMode())->GetSafeVectorWithinMapBounds(movementVector);

			FVector newLocation = FMath::Lerp(GetActorLocation(), safeMovementVector, MoveInterpFactor);

			SetActorLocation(newLocation);
			break;
		}
	}
}
