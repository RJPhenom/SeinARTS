#include "Components/SAFMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/OverlapResult.h"
#include "Engine/EngineTypes.h"
#include "Utils/SAFMathLibrary.h"
#include "DrawDebugHelpers.h"
#include "Debug/SAFDebugTool.h"

USAFMovementComponent::USAFMovementComponent() {
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	TurningBoost = 0.0f; // Disable turning boost by default

	// Configure plane constraint
	bConstrainToPlane = true;
	SetPlaneConstraintNormal(FVector::UpVector);
	bSnapToPlaneAtStart = true;

	// Initialize mode-specific defaults based on MovementMode
	ApplyMovementModeDefaults();
}

void USAFMovementComponent::ApplyMovementModeDefaults() {
	switch (MovementMode) {
		case ESAFMovementMode::Infantry:
			MaxSpeed 								= 250.0f;
			Acceleration 							= 1500.0f;
			Deceleration 							= 2000.0f;
			MaxRotationRate 						= 300.0f;
			ReverseEngageDotThreshold 				= 0.f;
			ReverseEngageDistanceThreshold 			= 0.f;
			ReverseMaxSpeed 						= 0.f;

			Infantry_bAllowStrafe 					= true;
			Infantry_StrafingDotThreshold 			= 0.3f;

			break;

		case ESAFMovementMode::Tracked:
			MaxSpeed 								= 500.0f;
			Acceleration 							= 100.0f;
			Deceleration 							= 200.0f;
			MaxRotationRate 						= 40.0f;
			ReverseEngageDotThreshold 				= -0.5f;
			ReverseEngageDistanceThreshold 			= 2500.f;
			ReverseMaxSpeed 						= 100.f;

			if (Tracked_ThrottleVsMisalignmentDeg.GetRichCurve()->GetNumKeys() == 0) {
				FRichCurve* Curve = Tracked_ThrottleVsMisalignmentDeg.GetRichCurve();
				Curve->AddKey(0.f,   1.f);
				Curve->AddKey(8.f,   0.4f);
				Curve->AddKey(15.f,  0.2f);
				Curve->AddKey(45.f,  0.1f);
				Curve->AddKey(90.f,  0.f);
				Curve->AddKey(180.f, 0.f);
			}

			break;

		case ESAFMovementMode::Wheeled:
			MaxSpeed 								= 500.0f;
			Acceleration 							= 250.0f;
			Deceleration 							= 500.0f;
			MaxRotationRate 						= 60.0f;
			ReverseEngageDotThreshold 				= -0.5f;
			ReverseEngageDistanceThreshold 			= 2500.f;
			ReverseMaxSpeed 						= 300.f;

			Wheeled_Wheelbase 						= 220.0f;
			Wheeled_MaxSteerAngleDeg 				= 60.0f;
			Wheeled_SteerResponse 					= 3.0f;

			break;

		case ESAFMovementMode::Hover:
			MaxSpeed 								= 500.0f;
			Acceleration 							= 250.0f;
			Deceleration 							= 500.0f;
			MaxRotationRate 						= 60.0f;
			ReverseEngageDotThreshold 				= -0.5f;
			ReverseEngageDistanceThreshold 			= 2500.0f;
			ReverseMaxSpeed 						= 300.0f;

			break;

		default: break;
	}
}

// AI Movement Interface
// ===============================================================================================
void USAFMovementComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) {
	DesiredMoveDirection = MoveVelocity.GetSafeNormal();
	DesiredMoveSpeed = bForceMaxSpeed ? MaxSpeed : MoveVelocity.Size();
	bHasMoveRequest = !DesiredMoveDirection.IsNearlyZero();
	if (bConstrainToPlane) DesiredMoveDirection = ConstrainToPlane(DesiredMoveDirection);
}

void USAFMovementComponent::StopActiveMovement() {
	bHasMoveRequest = false;
	DesiredMoveDirection = FVector::ZeroVector;
	DesiredMoveSpeed = 0.0f;
	Velocity = FVector::ZeroVector;
	CurrentSteerDeg = 0.0f;
}

// Core Tick / Movement Functions
// ==============================================================================================================================
void USAFMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!UpdatedComponent || !PawnOwner) return;
	PerformMovement(DeltaTime);
}

void USAFMovementComponent::PerformMovement(float DeltaTime) {
	if (!UpdatedComponent || !PawnOwner) return;
	switch (MovementMode) {
		case ESAFMovementMode::Infantry: PerformInfantryMovement(DeltaTime); break;
		case ESAFMovementMode::Tracked: PerformTrackedMovement(DeltaTime); break;
		case ESAFMovementMode::Wheeled: PerformWheeledMovement(DeltaTime); break;
		case ESAFMovementMode::Hover: PerformHoverMovement(DeltaTime); break;
		default: PerformBaseMovement(DeltaTime); break;
	}
}

void USAFMovementComponent::PerformBaseMovement(float DeltaTime) {
	// Original base implementation: simple floating pawn movement
	if (!bHasMoveRequest || DesiredMoveDirection.IsNearlyZero()) {
		// No movement requested, apply deceleration / stop movement
		if (!Velocity.IsNearlyZero()) {
			const FVector DecelerationVector = -Velocity.GetSafeNormal() * Deceleration * DeltaTime;
			FVector NewVelocity = Velocity + DecelerationVector;
			if (FVector::DotProduct(Velocity, NewVelocity) <= 0.0f) NewVelocity = FVector::ZeroVector;
			Velocity = ConstrainToPlane(NewVelocity);
		}

		return;
	}

	// Calculate target velocity
	const FVector 	TargetVelocity 	= DesiredMoveDirection * DesiredMoveSpeed;
	const FVector 	VelocityDelta 	= TargetVelocity - Velocity;

	// Handle acceleration towards target velocity
	if (!VelocityDelta.IsNearlyZero()) {
		const float AccelMagnitude = Acceleration * DeltaTime;
		FVector AccelerationVector = VelocityDelta.GetSafeNormal() * AccelMagnitude;

		// Don't overshoot the target
		if (AccelerationVector.SizeSquared() > VelocityDelta.SizeSquared()) AccelerationVector = VelocityDelta;

		Velocity += AccelerationVector;
		Velocity = ConstrainToPlane(Velocity);
		const float CurrentSpeed = Velocity.Size();
		if (CurrentSpeed > MaxSpeed) Velocity = Velocity * (MaxSpeed / CurrentSpeed);
	}

	// Apply movement
	if (!Velocity.IsNearlyZero()) {
		const FVector MovementDelta = Velocity * DeltaTime;
		const FRotator CurrentRotation = GetCurrentRotation();
		MoveUpdatedComponent(MovementDelta, CurrentRotation, false);
	}

	const FRotator 	CurrentRotation 	= GetCurrentRotation();
	const FRotator 	DesiredRotation 	= DesiredMoveDirection.Rotation();
	const float 	RotationStep 		= MaxRotationRate * DeltaTime;
	const float 	YawDelta 			= FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, DesiredRotation.Yaw);
	const float 	ClampedYawDelta 	= FMath::Clamp(YawDelta, -RotationStep, RotationStep);
	const FRotator 	NewRotation 		= FRotator(0.0f, CurrentRotation.Yaw + ClampedYawDelta, 0.0f);

	if (UpdatedComponent) UpdatedComponent->SetWorldRotation(NewRotation);
}

void USAFMovementComponent::PerformInfantryMovement(float DeltaTime) {
	if (!bHasMoveRequest || DesiredMoveDirection.IsNearlyZero()) {
		// No movement requested, apply deceleration / stop movement
		if (!Velocity.IsNearlyZero()) {
			const FVector DecelerationVector = -Velocity.GetSafeNormal() * Deceleration * DeltaTime;
			FVector NewVelocity = Velocity + DecelerationVector;
			if (FVector::DotProduct(Velocity, NewVelocity) <= 0.0f) NewVelocity = FVector::ZeroVector;
			Velocity = ConstrainToPlane(NewVelocity);
		}

		return;
	}

	// Calculate target velocity
	const FVector TargetVelocity = DesiredMoveDirection * DesiredMoveSpeed;
	const FVector VelocityDelta = TargetVelocity - Velocity;

	// Handle acceleration towards target velocity
	if (!VelocityDelta.IsNearlyZero()) {
		const float AccelMagnitude = Acceleration * DeltaTime;
		FVector AccelerationVector = VelocityDelta.GetSafeNormal() * AccelMagnitude;

		// Don't overshoot the target
		if (AccelerationVector.SizeSquared() > VelocityDelta.SizeSquared()) AccelerationVector = VelocityDelta;

		Velocity += AccelerationVector;
		Velocity = ConstrainToPlane(Velocity);
		const float CurrentSpeed = Velocity.Size();
		if (CurrentSpeed > MaxSpeed) Velocity = Velocity * (MaxSpeed / CurrentSpeed);
	}

	// Apply movement
	if (!Velocity.IsNearlyZero()) {
		const FVector MovementDelta = Velocity * DeltaTime;
		const FRotator CurrentRotation = GetCurrentRotation();
		MoveUpdatedComponent(MovementDelta, CurrentRotation, false);
	}

	// Handle Rotation
	const bool bHasTargetToFace = bUseDesiredFacing;
	const bool bShouldStrafe = bHasTargetToFace && Infantry_bAllowStrafe;

	// Check if movement direction is reasonable for strafing (not behind us)
	bool bCanStrafeInThisDirection = true;
	if (bShouldStrafe) {
		const FVector ForwardDir = GetCurrentRotation().Vector();
		const float DotProduct = FVector::DotProduct(ForwardDir, DesiredMoveDirection);
		bCanStrafeInThisDirection = DotProduct > Infantry_StrafingDotThreshold;
	}

	const bool bShouldFaceMovement = !bShouldStrafe || !bCanStrafeInThisDirection;
	if (bShouldFaceMovement) {
		const FRotator CurrentRotation = GetCurrentRotation();

		FRotator TargetRotation;
		if (bUseDesiredFacing) TargetRotation = FRotator(0.0f, DesiredFacingYaw, 0.0f);
		else TargetRotation = DesiredMoveDirection.Rotation();

		const float RotationStep 	= MaxRotationRate * DeltaTime;
		const float YawDelta 		= FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetRotation.Yaw);
		const float ClampedYawDelta = FMath::Clamp(YawDelta, -RotationStep, RotationStep);
		const FRotator NewRotation 	= FRotator(0.0f, CurrentRotation.Yaw + ClampedYawDelta, 0.0f);

		if (UpdatedComponent) UpdatedComponent->SetWorldRotation(NewRotation);
	}
}

void USAFMovementComponent::PerformTrackedMovement(float DeltaTime) {
	SAFDEBUG_DRAWARROWSPHERE(25.f, FColor::Turquoise);
	if (!UpdatedComponent) return;

	// Use base class movement state
	const FVector& DesiredMoveDir = GetDesiredMoveDirection();
	const bool bHasInput = HasMoveRequest();

	// Resolve directions
	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const FVector Desired = DesiredMoveDir.IsNearlyZero() ? Forward : DesiredMoveDir;

	// Reverse rule (applies when a goal was provided)
	bool bUseReverse = false;
	if (bHasReverseGoal) {
		const FVector ToGoal 				= (ReverseGoal - UpdatedComponent->GetComponentLocation());
		const float Dist2 					= ToGoal.SizeSquared2D();
		const float Dot 					= FVector::DotProduct(Forward, ToGoal.GetSafeNormal2D());
		const bool 	bBehindEnough 			= (Dot <= ReverseEngageDotThreshold);
		const bool 	bCloseEnough 			= (Dist2 <= FMath::Square(ReverseEngageDistanceThreshold));
					bUseReverse 			= bBehindEnough && bCloseEnough;
	}

	FVector 		DesiredDirection 		= bUseReverse ? -Desired : Desired;
	const float 	Yaw 					= UpdatedComponent->GetComponentRotation().Yaw;
	const float 	YawErr 					= FMath::FindDeltaAngleDegrees(Yaw, DesiredDirection.Rotation().Yaw);
	const float 	YawStep 				= FMath::Clamp(YawErr, -MaxRotationRate * DeltaTime, MaxRotationRate * DeltaTime);
	const FRotator 	NewRot 					= FRotator(0.f, Yaw + YawStep, 0.f);
	const float 	CosA 					= FVector::DotProduct(NewRot.Vector(), DesiredDirection);
	const float 	AngDeg 					= FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosA, -1.f, 1.f)));
	const float 	TargetSpeedUnsignedRaw 	= bUseReverse ? FMath::Min(ReverseMaxSpeed, MaxSpeed) : MaxSpeed * EvalTrackedThrottle(AngDeg);
	const float 	TargetSpeedUnsigned 	= bHasInput ? FMath::Min(TargetSpeedUnsignedRaw, GetDesiredMoveSpeed()) : 0.f;

	MoveUpdatedComponent(FVector::ZeroVector, NewRot, false);
	ApplyForwardVelocityMovement(DeltaTime, NewRot, bUseReverse, TargetSpeedUnsigned);
}

void USAFMovementComponent::PerformWheeledMovement(float DeltaTime) {
	SAFDEBUG_DRAWARROWSPHERE(25.f, FColor::Turquoise);
	if (!UpdatedComponent) return;

	// Use base class movement state
	const FVector& DesiredMoveDir = GetDesiredMoveDirection();
	const bool bHasInput = HasMoveRequest();

	// Resolve directions
	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const FVector Desired = DesiredMoveDir.IsNearlyZero() ? Forward : DesiredMoveDir;

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

	FVector 		DesiredDirection 		= bUseReverse ? -Desired : Desired;
	const float 	TargetSpeedUnsignedRaw 	= bUseReverse ? ReverseMaxSpeed : MaxSpeed;
	const float 	TargetSpeedUnsigned 	= bHasInput ? FMath::Min(TargetSpeedUnsignedRaw, GetDesiredMoveSpeed()) : 0.f;
	const float 	Yaw 					= UpdatedComponent->GetComponentRotation().Yaw;
	const float 	YawErr 					= FMath::FindDeltaAngleDegrees(Yaw, DesiredDirection.Rotation().Yaw);
	const float 	DesiredSteerDeg 		= FMath::Clamp(YawErr, -Wheeled_MaxSteerAngleDeg, Wheeled_MaxSteerAngleDeg);
					CurrentSteerDeg 		= FMath::FInterpTo(CurrentSteerDeg, DesiredSteerDeg, DeltaTime, Wheeled_SteerResponse);
	const float 	Speed 					= Velocity.Size2D();
	const float 	SteerRd 				= FMath::DegreesToRadians(CurrentSteerDeg);
	const float 	YawRateDeg 				= (FMath::IsNearlyZero(SteerRd) || Wheeled_Wheelbase <= 1.f) ? 0.f : FMath::RadiansToDegrees((Speed / Wheeled_Wheelbase) * FMath::Tan(SteerRd));
	const float 	YawStep 				= FMath::Clamp(YawRateDeg, -MaxRotationRate, MaxRotationRate) * DeltaTime;
	const FRotator 	NewRot					= FRotator(0.f, Yaw + YawStep, 0.f);

	MoveUpdatedComponent(FVector::ZeroVector, NewRot, false);
	ApplyForwardVelocityMovement(DeltaTime, NewRot, bUseReverse, TargetSpeedUnsigned);
}

void USAFMovementComponent::PerformHoverMovement(float DeltaTime) {
	SAFDEBUG_DRAWARROWSPHERE(25.f, FColor::Turquoise);
	if (!UpdatedComponent) return;

	// Use base class movement state
	const FVector& DesiredMoveDir = GetDesiredMoveDirection();
	const bool bHasInput = HasMoveRequest();

	// Resolve directions
	const FVector Forward = UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	const FVector Desired = DesiredMoveDir.IsNearlyZero() ? Forward : DesiredMoveDir;

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

	// Hover movement - similar to tracked but with different physics
	FVector 		DesiredDirection 		= bUseReverse ? -Desired : Desired;
	const float 	TargetSpeedUnsignedRaw 	= bUseReverse ? ReverseMaxSpeed : MaxSpeed;
	const float 	TargetSpeedUnsigned 	= bHasInput ? FMath::Min(TargetSpeedUnsignedRaw, GetDesiredMoveSpeed()) : 0.f;
	const float 	Yaw 					= UpdatedComponent->GetComponentRotation().Yaw;
	const float 	YawErr 					= FMath::FindDeltaAngleDegrees(Yaw, DesiredDirection.Rotation().Yaw);
	const float 	YawStep 				= FMath::Clamp(YawErr, -MaxRotationRate * DeltaTime, MaxRotationRate * DeltaTime);
	const FRotator 	NewRot					= FRotator(0.f, Yaw + YawStep, 0.f);
	
	MoveUpdatedComponent(FVector::ZeroVector, NewRot, false);
	ApplyForwardVelocityMovement(DeltaTime, NewRot, bUseReverse, TargetSpeedUnsigned);
}

// Utility Functions
// ==================================================================================================
float USAFMovementComponent::EvalTrackedThrottle(float MisalignmentDeg) const {
	const FRichCurve* Curve = Tracked_ThrottleVsMisalignmentDeg.GetRichCurveConst();
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

/** Apply forward velocity movement based on the current state. */
void USAFMovementComponent::ApplyForwardVelocityMovement(float DeltaTime, FRotator NewRot, bool bUseReverse, float TargetSpeedUnsigned) {
	// If we're targeting zero speed, don't translate this frame (prevents inching)
	if (TargetSpeedUnsigned <= KINDA_SMALL_NUMBER) {
		Velocity.X = 0.f;
		Velocity.Y = 0.f;
		return;
	}

	const FVector 	NewForward 		= NewRot.Vector();
	const float 	CurrSigned 		= FVector::DotProduct(Velocity, NewForward);
	const float 	TargetSigned 	= bUseReverse ? -TargetSpeedUnsigned : TargetSpeedUnsigned;
	const float 	NewSigned 		= SAFMathLibrary::SmoothStepForwardVelocity(DeltaTime, CurrSigned, TargetSigned, Acceleration, Deceleration);
	const FVector 	NewVelocity 	= NewForward * NewSigned;
	const FVector 	CurrLoc 		= UpdatedComponent->GetComponentLocation();
	const FVector 	Proposed 		= CurrLoc + NewVelocity * DeltaTime;

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

#if WITH_EDITOR
void USAFMovementComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() 
		== GET_MEMBER_NAME_CHECKED(USAFMovementComponent, MovementMode))
		ApplyMovementModeDefaults();
}
#endif