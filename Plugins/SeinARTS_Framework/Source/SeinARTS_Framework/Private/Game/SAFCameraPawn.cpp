


#include "Game/SAFCameraPawn.h"
#include "Kismet/KismetMathLibrary.h"


ASAFCameraPawn::ASAFCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Component setup
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmComponent"));
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));

	RootComponent = SpringArmComponent;
	CameraComponent->SetupAttachment(SpringArmComponent);

	SpringArmComponent->bUsePawnControlRotation = true;
}

void ASAFCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	isFreeCam = FreeCam;
	AddControllerPitchInput(DefaultAngle);
}

void ASAFCameraPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Following is handle in tick because its inexpensive.
	if (GetFollowMode()) {
		SetActorLocation(FollowTarget->GetActorLocation());
	}

}

void ASAFCameraPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}


// ===============================
//       CAMERA CONTROLS
// ===============================

void ASAFCameraPawn::SnapCamera(FVector Vector, FVector LookAtVector, float Zoom) {
	FVector SafeVector = Cast<ASAFGameModeBase>(GetWorld()->GetAuthGameMode())->GetSafeVectorWithinMapBounds(Vector);
	
	SetActorLocation(SafeVector);
	if (LookAtVector != FVector::ZeroVector) SnapCameraRotation(LookAtVector);
	if (Zoom != 0.0f) SnapCameraZoom(Zoom);

	OnCameraSnap.Broadcast();
}

void ASAFCameraPawn::PanCamera(float PanX, float PanY, bool Fast) {
	ClearFollowTarget();

	// Build movement input
	float ScaleX = PanX * MoveSpeed;
	float ScaleY = PanY * MoveSpeed;

	if (MapMode) {
		ScaleX *= MoveCoefficient;
		ScaleY *= MoveCoefficient;
	}

	if (Fast) {
		ScaleX *= MoveFastCoefficient;
		ScaleY *= MoveFastCoefficient;
	}

	FRotator ControlRot = GetControlRotation();
	FVector RightVector = UKismetMathLibrary::GetRightVector(ControlRot);
	FVector ForwardVector = UKismetMathLibrary::Cross_VectorVector(RightVector, FVector::UpVector);

	AddMovementInput(RightVector, ScaleX);
	AddMovementInput(ForwardVector, ScaleY);

	// Consume and wrap in bounds, then move
	FVector MovementVector = ConsumeMovementInputVector();
	FVector SafeMovementVector = Cast<ASAFGameModeBase>(GetWorld()->GetAuthGameMode())->GetSafeVectorWithinMapBounds(MovementVector);

	SetActorLocation(SafeMovementVector);
}

void ASAFCameraPawn::PanCameraAltitude(float PanZ, bool Fast) {
	ClearFollowTarget();

	// Build input
	float ScaleZ = PanZ * MoveSpeed;

	if (MapMode) ScaleZ *= MoveCoefficient;
	if (Fast) ScaleZ *= MoveFastCoefficient;

	AddMovementInput(FVector::UpVector, ScaleZ);

	// Consume and wrap in bounds
	FVector MovementVector = ConsumeMovementInputVector();
	FVector SafeMovementVector = Cast<ASAFGameModeBase>(GetWorld()->GetAuthGameMode())->GetSafeVectorWithinMapBounds(MovementVector);

	SetActorLocation(SafeMovementVector);
}

void ASAFCameraPawn::SnapCameraRotation(FVector LookAtVector, FRotator Rotator) {
	if (Rotator != FRotator::ZeroRotator) {
		GetController()->SetControlRotation(Rotator);
	}

	else {
		FRotator LookRotation = UKismetMathLibrary::FindLookAtRotation(GetPawnViewLocation(), LookAtVector);
		GetController()->SetControlRotation(LookRotation);
	}

	OnCameraSnap.Broadcast();
}

void ASAFCameraPawn::RotateCamera(float Pitch, float Yaw) {
	float ScalePitch = Pitch * RotationSpeed;
	float ScaleYaw = Yaw * RotationSpeed;

	AddControllerPitchInput(ScalePitch);
	AddControllerYawInput(ScaleYaw);
}

FVector2d ASAFCameraPawn::GetCameraZoomBounds() {
	float MinZoom = (MapMode) ? ZoomMin * ZoomCoefficient : ZoomMin;
	float MaxZoom = (MapMode) ? ZoomMax * ZoomCoefficient : ZoomMax;

	return FVector2D(MinZoom, MaxZoom);
}

bool ASAFCameraPawn::CheckZoomWithinBounds(float Zoom) {
	FVector2D ZoomBounds = GetCameraZoomBounds();

	return ZoomBounds.X < Zoom && Zoom < ZoomBounds.Y;
}

void ASAFCameraPawn::SnapCameraZoom(float Zoom) {
	if (CheckZoomWithinBounds(Zoom)) SpringArmComponent->TargetArmLength = Zoom;

	OnCameraSnap.Broadcast();
}

void ASAFCameraPawn::ZoomCamera(float Zoom) {
	float ScaleZoom = Zoom * ZoomSpeed;
	if (MapMode) ScaleZoom *= ZoomCoefficient;

	// Default Mode
	if (!isFreeCam) {
		ScaleZoom *= -1; // Inverted because we subtract from springarm length as we 'zoom in'

		float ZoomTarget = ScaleZoom + SpringArmComponent->TargetArmLength;

		if (CheckZoomWithinBounds(ZoomTarget)) {
			SpringArmComponent->TargetArmLength = ZoomTarget;
		}
	}

	// FreeCam
	else {
		FRotator ControlRot = GetControlRotation();
		FVector ZoomVector = UKismetMathLibrary::GetForwardVector(ControlRot);

		AddMovementInput(ZoomVector, ScaleZoom);
	}
}


// ===============================
//     TOGGLES & CAMERA MODES 
// ===============================

void ASAFCameraPawn::ToggleMapMode() {
	MapMode = !MapMode;

	// Default mode
	if (!isFreeCam) {

		if (MapMode) {
			StoredLocalZoom = SpringArmComponent->TargetArmLength;
			SpringArmComponent->TargetArmLength = MapModeOffset;
		}

		else {
			SpringArmComponent->TargetArmLength = StoredLocalZoom;
		}

	}

	// FreeCam
	else {
		ZoomCamera(MapMode ? MapModeOffset * -1 : MapModeOffset);
	}
}
