#include "Components/SAFInfantryMovementComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"

USAFInfantryMovementComponent::USAFInfantryMovementComponent() {
	// Set infantry-specific defaults directly to inherited properties
	MaxSpeed = 600.0f;
	Acceleration = 3000.0f;  // High acceleration for responsive movement
	Deceleration = 4000.0f;  // High deceleration for quick stops
	MaxRotationRate = 720.0f;        // Fast rotation for infantry

	// Enable XY plane constraint by default for RTS movement (use inherited property)
	bConstrainToPlane = true;
}

void USAFInfantryMovementComponent::SetDesiredFacing(float InYaw) {
	DesiredFacingYaw = InYaw;
	bUseDesiredFacing = true;
}

void USAFInfantryMovementComponent::ClearDesiredFacing() {
	bUseDesiredFacing = false;
}

void USAFInfantryMovementComponent::SetFormationPosition(const FVector& InFormationPosition) {
	FormationPosition = InFormationPosition;
	bHasFormationPosition = true;
}

void USAFInfantryMovementComponent::ClearFormationPosition() {
	bHasFormationPosition = false;
	bIsInFormation = true; // Reset to in-formation when no formation is set
}

void USAFInfantryMovementComponent::PerformMovement(float DeltaTime) {
	if (!UpdatedComponent || !PawnOwner) {
		return;
	}

	// Apply formation logic first
	ApplyFormationLogic(DeltaTime);

	// Apply infantry-specific movement
	ApplyInfantryMovement(DeltaTime);
}

void USAFInfantryMovementComponent::PerformRotation(float DeltaTime) {
	if (!PawnOwner) {
		return;
	}

	const FRotator CurrentRotation = GetCurrentRotation();
	FRotator TargetRotation = CurrentRotation;

	// Determine target rotation based on movement mode
	if (bUseDesiredFacing) {
		// Use specific desired facing
		TargetRotation = FRotator(0.0f, DesiredFacingYaw, 0.0f);
	} else if (!bAllowStrafe && !Velocity.IsNearlyZero(1.0f)) {
		// Face movement direction when not strafing
		const FVector MovementDir = ConstrainToPlane(Velocity).GetSafeNormal();
		if (!MovementDir.IsNearlyZero()) {
			TargetRotation = MovementDir.Rotation();
		}
	}
	// If bAllowStrafe is true and no desired facing, maintain current rotation

	// Apply rotation interpolation
	const float RotationStep = FacingRotationRate * DeltaTime;
	const float YawDelta = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, TargetRotation.Yaw);
	const float ClampedYawDelta = FMath::Clamp(YawDelta, -RotationStep, RotationStep);

	const FRotator NewRotation = FRotator(0.0f, CurrentRotation.Yaw + ClampedYawDelta, 0.0f);
	PawnOwner->SetActorRotation(NewRotation);
}

void USAFInfantryMovementComponent::ApplyInfantryMovement(float DeltaTime) {
	const FVector InputDirection = GetInputDirection();
	
	// Calculate base acceleration
	FVector BaseAcceleration = CalculateMovementAcceleration(InputDirection, DeltaTime);

	// Apply separation forces
	if (bEnableSeparation) {
		ApplySeparation(BaseAcceleration, DeltaTime);
	}

	// Apply acceleration to velocity
	FVector NewVelocity = Velocity + BaseAcceleration * DeltaTime;

	// Apply braking when no input
	if (ShouldApplyBraking()) {
		const float BrakingDecel = BrakingFriction * Deceleration * DeltaTime;
		if (NewVelocity.SizeSquared() > 0.0f) {
			const FVector BrakingForce = -NewVelocity.GetSafeNormal() * BrakingDecel;
			NewVelocity += BrakingForce * DeltaTime;

			// Stop if we would overshoot zero
			if (FVector::DotProduct(Velocity, NewVelocity) <= 0.0f) {
				NewVelocity = FVector::ZeroVector;
			}
		}
	}

	// Constrain to XY plane
	NewVelocity = ConstrainToPlane(NewVelocity);

	// Apply speed limits
	const float CurrentMaxSpeed = CalculateCurrentMaxSpeed();
	const float CurrentSpeed = NewVelocity.Size();
	if (CurrentSpeed > CurrentMaxSpeed) {
		NewVelocity = NewVelocity * (CurrentMaxSpeed / CurrentSpeed);
	}

	// Apply movement
	if (!NewVelocity.IsNearlyZero()) {
		const FVector MovementDelta = NewVelocity * DeltaTime;
		const FRotator CurrentRotation = GetCurrentRotation();

		// Use collision-aware movement
		FHitResult Hit;
		SafeMoveUpdatedComponent(MovementDelta, CurrentRotation.Quaternion(), true, Hit);
		
		if (Hit.IsValidBlockingHit()) {
			// Slide along surfaces when hitting obstacles
			SlideAlongSurface(MovementDelta, 1.0f - Hit.Time, Hit.Normal, Hit);
		}

		// Update velocity based on actual movement
		const FVector ActualDelta = UpdatedComponent->GetComponentLocation() - (UpdatedComponent->GetComponentLocation() - MovementDelta);
		Velocity = ActualDelta / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
		Velocity = ConstrainToPlane(Velocity);
	} else {
		Velocity = FVector::ZeroVector;
	}

	// Update component velocity for networking
	UpdateComponentVelocity();
}

void USAFInfantryMovementComponent::ApplySeparation(FVector& OutAcceleration, float DeltaTime) {
	if (!UpdatedComponent || SeparationRadius <= 0.0f || SeparationStrength <= 0.0f) {
		return;
	}

	UWorld* World = GetWorld();
	if (!World) {
		return;
	}

	const FVector MyLocation = UpdatedComponent->GetComponentLocation();

	// Set up collision query
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(SeparationChannel);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SAFInfantrySeparation), false, PawnOwner);
	QueryParams.bTraceComplex = false;
	QueryParams.AddIgnoredActor(PawnOwner);

	const FCollisionShape SphereShape = FCollisionShape::MakeSphere(SeparationRadius);

	// Query for nearby units
	TArray<FOverlapResult> OverlapResults;
	World->OverlapMultiByObjectType(OverlapResults, MyLocation, FQuat::Identity, ObjectParams, SphereShape, QueryParams);

	// Calculate separation force
	FVector SeparationForce = FVector::ZeroVector;
	int32 NeighborCount = 0;

	for (const FOverlapResult& Result : OverlapResults) {
		const UPrimitiveComponent* OtherComponent = Result.Component.Get();
		const AActor* OtherActor = Result.GetActor();
		
		if (!OtherComponent || !OtherActor || OtherActor == PawnOwner) {
			continue;
		}

		// Calculate separation direction and strength
		const FVector OtherLocation = OtherComponent->GetComponentLocation();
		const FVector SeparationDirection = MyLocation - OtherLocation;
		const float Distance = FMath::Max(SeparationDirection.Size(), 1.0f);
		
		// Stronger force when closer
		const float ForceMultiplier = FMath::Max(0.0f, 1.0f - (Distance / SeparationRadius));
		SeparationForce += SeparationDirection.GetSafeNormal() * ForceMultiplier;
		++NeighborCount;
	}

	// Apply average separation force
	if (NeighborCount > 0) {
		SeparationForce /= static_cast<float>(NeighborCount);
		SeparationForce = ConstrainToPlane(SeparationForce);

		// Scale by separation strength
		FVector SeparationAcceleration = SeparationForce * SeparationStrength;

		// Limit maximum separation speed
		const FVector SeparationVelocityDelta = SeparationAcceleration * DeltaTime;
		const float SeparationSpeed = SeparationVelocityDelta.Size();
		if (SeparationSpeed > MaxSeparationSpeed) {
			SeparationAcceleration = SeparationVelocityDelta.GetSafeNormal() * (MaxSeparationSpeed / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
		}

		OutAcceleration += SeparationAcceleration;
	}
}

void USAFInfantryMovementComponent::ApplyFormationLogic(float DeltaTime) {
	if (!bUseFormationMovement || !bHasFormationPosition) {
		bIsInFormation = true;
		return;
	}

	// Check if we're in formation
	const FVector CurrentLocation = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const float DistanceToFormation = FVector::Dist2D(CurrentLocation, FormationPosition);
	bIsInFormation = DistanceToFormation <= FormationTolerance;
}

FVector USAFInfantryMovementComponent::CalculateMovementAcceleration(const FVector& InputDirection, float DeltaTime) const {
	if (InputDirection.IsNearlyZero(1e-3f)) {
		// No input - apply deceleration if moving
		if (!Velocity.IsNearlyZero(1e-3f)) {
			FVector DecelerationVector = -Velocity.GetSafeNormal() * Deceleration;
			
			// Check if we should stop completely this frame
			const float StopTime = Velocity.Size() / Deceleration;
			if (StopTime <= DeltaTime) {
				// Stop completely
				DecelerationVector = -Velocity / DeltaTime;
			}
			
			return ConstrainToPlane(DecelerationVector);
		}
		return FVector::ZeroVector;
	}

	// Apply acceleration in input direction
	return ConstrainToPlane(InputDirection.GetSafeNormal() * Acceleration);
}

float USAFInfantryMovementComponent::CalculateCurrentMaxSpeed() const {
	float CurrentMaxSpeed = MaxSpeed;

	// Reduce speed when out of formation
	if (bUseFormationMovement && !bIsInFormation) {
		CurrentMaxSpeed *= OutOfFormationSpeedMultiplier;
	}

	return CurrentMaxSpeed;
}

FVector USAFInfantryMovementComponent::GetInputDirection() const {
	// Use the desired movement direction from base class
	return GetDesiredMoveDirection();
}

bool USAFInfantryMovementComponent::ShouldApplyBraking() const {
	// Apply braking when no movement input
	return !HasMoveRequest() || GetDesiredMoveDirection().IsNearlyZero(1e-3f);
}
