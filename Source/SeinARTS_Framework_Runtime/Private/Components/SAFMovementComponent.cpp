#include "Components/SAFMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"

USAFMovementComponent::USAFMovementComponent() {
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Set base floating pawn movement properties (inherited)
	MaxSpeed = 600.0f;
	Acceleration = 2048.0f;
	Deceleration = 2048.0f;
	TurningBoost = 0.0f; // Disable turning boost by default

	// Configure plane constraint for RTS movement (use inherited bConstrainToPlane)
	bConstrainToPlane = true;
	SetPlaneConstraintNormal(FVector::UpVector);
	bSnapToPlaneAtStart = true;
}

void USAFMovementComponent::RequestDirectMove(const FVector& MoveVelocity, bool bForceMaxSpeed) {
	// Extract direction and speed from AI request
	DesiredMoveDirection = MoveVelocity.GetSafeNormal();
	DesiredMoveSpeed = bForceMaxSpeed ? MaxSpeed : MoveVelocity.Size();
	bHasMoveRequest = !DesiredMoveDirection.IsNearlyZero();

	// Constrain to XY plane if required
	if (bConstrainToPlane) {
		DesiredMoveDirection = ConstrainToPlane(DesiredMoveDirection);
	}
}

void USAFMovementComponent::StopActiveMovement() {
	bHasMoveRequest = false;
	DesiredMoveDirection = FVector::ZeroVector;
	DesiredMoveSpeed = 0.0f;
	Velocity = FVector::ZeroVector;
}

bool USAFMovementComponent::IsMoving() const {
	return Velocity.SizeSquared() > FMath::Square(StopSpeedThreshold);
}

void USAFMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!UpdatedComponent || !PawnOwner) {
		return;
	}

	// Update base class properties from our settings (already inherited, no need to sync)
	// MaxSpeed, Acceleration, Deceleration are the authoritative properties

	// Perform movement and rotation
	PerformMovement(DeltaTime);
	PerformRotation(DeltaTime);
}

void USAFMovementComponent::PerformMovement(float DeltaTime) {
	// Base implementation: simple floating pawn movement
	if (!bHasMoveRequest || DesiredMoveDirection.IsNearlyZero()) {
		// No movement requested, apply deceleration
		if (!Velocity.IsNearlyZero()) {
			const FVector DecelerationVector = -Velocity.GetSafeNormal() * Deceleration * DeltaTime;
			FVector NewVelocity = Velocity + DecelerationVector;

			// Stop if we'd overshoot zero
			if (FVector::DotProduct(Velocity, NewVelocity) <= 0.0f) {
				NewVelocity = FVector::ZeroVector;
			}

			Velocity = ConstrainToPlane(NewVelocity);
		}
		return;
	}

	// Calculate target velocity
	const FVector TargetVelocity = DesiredMoveDirection * DesiredMoveSpeed;
	const FVector VelocityDelta = TargetVelocity - Velocity;

	// Apply acceleration towards target velocity
	if (!VelocityDelta.IsNearlyZero()) {
		const float AccelMagnitude = Acceleration * DeltaTime;
		FVector AccelerationVector = VelocityDelta.GetSafeNormal() * AccelMagnitude;

		// Don't overshoot the target
		if (AccelerationVector.SizeSquared() > VelocityDelta.SizeSquared()) {
			AccelerationVector = VelocityDelta;
		}

		Velocity += AccelerationVector;
		Velocity = ConstrainToPlane(Velocity);

		// Clamp to max speed
		const float CurrentSpeed = Velocity.Size();
		if (CurrentSpeed > MaxSpeed) {
			Velocity = Velocity * (MaxSpeed / CurrentSpeed);
		}
	}

	// Apply movement
	if (!Velocity.IsNearlyZero()) {
		const FVector MovementDelta = Velocity * DeltaTime;
		const FRotator CurrentRotation = GetCurrentRotation();
		SafePerformMovement(MovementDelta, CurrentRotation, DeltaTime);
	}
}

void USAFMovementComponent::PerformRotation(float DeltaTime) {
	// Base implementation: rotate towards movement direction
	if (!bHasMoveRequest || DesiredMoveDirection.IsNearlyZero()) {
		return;
	}

	const FRotator CurrentRotation = GetCurrentRotation();
	const FRotator DesiredRotation = CalculateDesiredRotation();

	// Calculate rotation step
	const float RotationStep = MaxRotationRate * DeltaTime;
	const float YawDelta = FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, DesiredRotation.Yaw);
	const float ClampedYawDelta = FMath::Clamp(YawDelta, -RotationStep, RotationStep);

	// Apply rotation
	const FRotator NewRotation = FRotator(0.0f, CurrentRotation.Yaw + ClampedYawDelta, 0.0f);
	if (UpdatedComponent) {
		UpdatedComponent->SetWorldRotation(NewRotation);
	}
}

bool USAFMovementComponent::ProjectToNavMesh(const FVector& WorldLocation, FVector& OutProjectedLocation) const {
	if (!bProjectToNavMesh) {
		OutProjectedLocation = WorldLocation;
		return true;
	}

	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld())) {
		FNavLocation NavLocation;
		const ANavigationData* NavData = NavSys->GetNavDataForProps(GetNavAgentPropertiesRef());

		if (NavSys->ProjectPointToNavigation(WorldLocation, NavLocation, NavProjectionExtent, NavData)) {
			OutProjectedLocation = NavLocation.Location;
			return true;
		}
	}

	// Fallback to original location if projection fails
	OutProjectedLocation = WorldLocation;
	return false;
}

void USAFMovementComponent::SafePerformMovement(const FVector& Delta, const FRotator& NewRotation, float DeltaTime) {
	if (!UpdatedComponent) {
		return;
	}

	// Calculate target location
	const FVector CurrentLocation = UpdatedComponent->GetComponentLocation();
	const FVector TargetLocation = CurrentLocation + Delta;

	// Project to navmesh if enabled
	FVector FinalLocation;
	if (ProjectToNavMesh(TargetLocation, FinalLocation)) {
		const FVector SafeDelta = FinalLocation - CurrentLocation;
		MoveUpdatedComponent(SafeDelta, NewRotation, false);
	} else {
		// If projection fails, don't move this frame to prevent falling off navmesh
		MoveUpdatedComponent(FVector::ZeroVector, NewRotation, false);
	}
}

FVector USAFMovementComponent::GetForwardDirection() const {
	if (UpdatedComponent) {
		return UpdatedComponent->GetForwardVector();
	}
	return FVector::ForwardVector;
}

FRotator USAFMovementComponent::GetCurrentRotation() const {
	if (UpdatedComponent) {
		return UpdatedComponent->GetComponentRotation();
	}
	return FRotator::ZeroRotator;
}

FRotator USAFMovementComponent::CalculateDesiredRotation() const {
	if (!DesiredMoveDirection.IsNearlyZero()) {
		return DesiredMoveDirection.Rotation();
	}
	return GetCurrentRotation();
}

FVector USAFMovementComponent::ConstrainToPlane(const FVector& Vector) const {
	if (bConstrainToPlane) {
		return FVector(Vector.X, Vector.Y, 0.0f);
	}
	return Vector;
}