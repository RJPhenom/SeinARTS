#include "Components/SAFVehicleMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Utils/SAFMathLibrary.h"
#include "DrawDebugHelpers.h"
#include "Debug/SAFDebugTool.h"

USAFVehicleMovementComponent::USAFVehicleMovementComponent() {
	// Set vehicle-specific defaults directly to inherited properties
	MaxSpeed = 500.0f;
	Acceleration = 250.0f;
	Deceleration = 500.0f;
	MaxRotationRate = 60.0f; // Slower rotation for vehicles
	TurningBoost = 0.0f;
}

void USAFVehicleMovementComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) {
	// Call base implementation to handle standard movement request
	Super::RequestDirectMove(MoveVelocity, bForceMaxSpeed);
}

void USAFVehicleMovementComponent::StopActiveMovement() {
	// Call base implementation and reset vehicle-specific state
	Super::StopActiveMovement();
	CurrentSteerDeg = 0.0f;
}

void USAFVehicleMovementComponent::PerformMovement(float DeltaTime) {
	SAFDEBUG_DRAWARROWSPHERE(25.f, FColor::Turquoise);
	if (!UpdatedComponent) return;

	// Use base class movement state instead of local variables
	const FVector& DesiredMoveDir = GetDesiredMoveDirection();
	const float& DesiredSpeed = GetDesiredMoveSpeed();
	const bool bHasInput = HasMoveRequest();

	// Resolve directions
	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const FVector Desired = DesiredMoveDir.IsNearlyZero() ? Forward : DesiredMoveDir;
	const float Yaw = UpdatedComponent->GetComponentRotation().Yaw;

	// Reverse rule (applies when a goal was provided)
	bool bUseReverse = false;
	if (bHasReverseGoal) {
		const FVector ToGoal = (ReverseGoal - UpdatedComponent->GetComponentLocation());
		const float Dist2 = ToGoal.SizeSquared2D();
		const float Dot = FVector::DotProduct(Forward, ToGoal.GetSafeNormal2D());
		const bool bBehindEnough = (Dot <= ReverseEngageDotThreshold);
		const bool bCloseEnough = (Dist2 <= FMath::Square(ReverseEngageDistanceThreshold));
		bUseReverse = bBehindEnough && bCloseEnough;
	}

	// Switch drive method based on type enum
	switch (DriveType) {
		case ESAFVehicleDriveType::Tracked: TickTrackedMovement(DeltaTime, bUseReverse, Forward, Desired, Yaw); break;
		case ESAFVehicleDriveType::Wheeled: TickWheeledMovement(DeltaTime, bUseReverse, Forward, Desired, Yaw); break;
		default: SAFDEBUG_ERROR("PerformMovement: encountered invalid DriveType."); break;
	}
}

void USAFVehicleMovementComponent::PerformRotation(float DeltaTime) {
	// Rotation is handled within the drive-specific movement methods
	// This is intentionally empty as vehicles handle rotation differently than the base class
}

// Steering
// ==================================================================================================================================
/** Checks the tracked forward velocity curve and returns the output for the misalignment input point. If no curve is set, sets the 
 * default curve (once) and returns the check against the default curve. */
float USAFVehicleMovementComponent::EvalTrackedThrottle(float MisalignmentDeg) const {
	const FRichCurve* Curve = ThrottleVsMisalignmentDeg.GetRichCurveConst();
	if (Curve && Curve->GetNumKeys() > 0) return FMath::Clamp(Curve->Eval(MisalignmentDeg), 0.f, 1.f);

	// Fallback hard-coded curve
	static FRichCurve DefaultCurve;
	static bool bInit = false;
	if (!bInit) {
		DefaultCurve.AddKey(0.f,   1.f);
		DefaultCurve.AddKey(8.f,   0.4f);
		DefaultCurve.AddKey(15.f,  0.2f);
		DefaultCurve.AddKey(45.f,  0.1f);
		DefaultCurve.AddKey(90.f,  0.f);
		DefaultCurve.AddKey(180.f, 0.f);
		bInit = true;
	}

	return FMath::Clamp(DefaultCurve.Eval(MisalignmentDeg), 0.f, 1.f);
}

/** Ticks movement if drive type is set to Tracked. */
void USAFVehicleMovementComponent::TickTrackedMovement(float DeltaTime, bool bUseReverse, FVector /*Forward*/, FVector LookAtDirection, float Yaw) {
	FVector DesiredDirection = bUseReverse ? -LookAtDirection : LookAtDirection;

	// Use curve to determine fwd throttle based on misalignment
	const float YawErr  = FMath::FindDeltaAngleDegrees(Yaw, DesiredDirection.Rotation().Yaw);
	const float YawStep = FMath::Clamp(YawErr, -MaxTurnRateDeg * DeltaTime, MaxTurnRateDeg * DeltaTime);
	const FRotator NewRot(0.f, Yaw + YawStep, 0.f);

	// Throttle vs misalignment (angle between facing and desired)
	const float CosA   = FVector::DotProduct(NewRot.Vector(), DesiredDirection);
	const float AngDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosA, -1.f, 1.f)));
	const float TargetSpeedUnsignedRaw = bUseReverse ? FMath::Min(ReverseMaxSpeed, MaxSpeed) : MaxSpeed * EvalTrackedThrottle(AngDeg);

	// *** Use base class movement state instead of local variables ***
	const bool bHasInput = HasMoveRequest() && GetDesiredMoveSpeed() > KINDA_SMALL_NUMBER;
	const float TargetSpeedUnsigned = bHasInput ? FMath::Min(TargetSpeedUnsignedRaw, GetDesiredMoveSpeed()) : 0.f;

	MoveUpdatedComponent(FVector::ZeroVector, NewRot, false);
	ApplyTickMovement(DeltaTime, NewRot, bUseReverse, TargetSpeedUnsigned);
}

/** Ticks movement if drive type is set to Wheeled. */
void USAFVehicleMovementComponent::TickWheeledMovement(float DeltaTime, bool bUseReverse, FVector /*Forward*/, FVector LookAtDirection, float Yaw) {
	FVector DesiredDirection = bUseReverse ? -LookAtDirection : LookAtDirection;

	// Wheeled steering, turning based on data dimensions
	const float YawErr = FMath::FindDeltaAngleDegrees(Yaw, DesiredDirection.Rotation().Yaw);
	const float DesiredSteerDeg = FMath::Clamp(YawErr, -MaxSteerAngleDeg, MaxSteerAngleDeg);
	CurrentSteerDeg = FMath::FInterpTo(CurrentSteerDeg, DesiredSteerDeg, DeltaTime, SteerResponse);

	const float Speed   = Velocity.Size2D();
	const float SteerRd = FMath::DegreesToRadians(CurrentSteerDeg);
	const float YawRateDeg = (FMath::IsNearlyZero(SteerRd) || Wheelbase <= 1.f) ? 0.f : FMath::RadiansToDegrees((Speed / Wheelbase) * FMath::Tan(SteerRd));
	const float YawStep = FMath::Clamp(YawRateDeg, -MaxTurnRateDeg, MaxTurnRateDeg) * DeltaTime;
	const float TargetSpeedUnsignedRaw = bUseReverse ? ReverseMaxSpeed : MaxSpeed;

	// Use base class movement state
	const bool bHasInput = HasMoveRequest() && GetDesiredMoveSpeed() > KINDA_SMALL_NUMBER;
	const float TargetSpeedUnsigned = bHasInput ? FMath::Min(TargetSpeedUnsignedRaw, GetDesiredMoveSpeed()) : 0.f;

	const FRotator NewRot(0.f, Yaw + YawStep, 0.f);
	MoveUpdatedComponent(FVector::ZeroVector, NewRot, false);
	ApplyTickMovement(DeltaTime, NewRot, bUseReverse, TargetSpeedUnsigned);
}

/** Applies update calculations to the actual velocity of the movement component. */
void USAFVehicleMovementComponent::ApplyTickMovement(float DeltaTime, FRotator NewRot, bool bUseReverse, float TargetSpeedUnsigned) {
	// If we're targeting zero speed, don't translate this frame (prevents inching)
	if (TargetSpeedUnsigned <= KINDA_SMALL_NUMBER) {
		Velocity.X = 0.f;
		Velocity.Y = 0.f;
		return;
	}

	const FVector NewForward = NewRot.Vector();

	const float CurrSigned = FVector::DotProduct(Velocity, NewForward);
	const float TargetSigned = bUseReverse ? -TargetSpeedUnsigned : TargetSpeedUnsigned;
	const float NewSigned = SAFMathLibrary::SmoothStepForwardVelocity(DeltaTime, CurrSigned, TargetSigned, Acceleration, Deceleration);

	const FVector NewVelocity = NewForward * NewSigned;
	const FVector CurrLoc  = UpdatedComponent->GetComponentLocation();
	const FVector Proposed = CurrLoc + NewVelocity * DeltaTime;

	// Project to navmesh (avoid running off navedges)
	bool bDidMove = false;
	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld())) {
		FNavLocation OnNav;
		const FVector QueryExtent(100.f, 100.f, 500.f);
		const ANavigationData* NavData = NavSys->GetNavDataForProps(GetNavAgentPropertiesRef());

		if (NavSys->ProjectPointToNavigation(Proposed, OnNav, QueryExtent, NavData)) {
			const FVector Delta = OnNav.Location - CurrLoc;
			MoveUpdatedComponent(Delta, NewRot, false);
			bDidMove = true;
		}
	}

	// If projection failed, still apply rotation (already applied by callers), but ensure no translation
	if (!bDidMove) { MoveUpdatedComponent(FVector::ZeroVector, NewRot, false); }

	Velocity.X = NewVelocity.X;
	Velocity.Y = NewVelocity.Y;
}
