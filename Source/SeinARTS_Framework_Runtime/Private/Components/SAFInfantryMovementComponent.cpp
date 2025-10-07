#include "Components/SAFInfantryMovementComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "GameFramework/Pawn.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"

USAFInfantryMovementComponent::USAFInfantryMovementComponent() {
	bConstrainToPlane = true;
	SetPlaneConstraintNormal(FVector::UpVector);
	bSnapToPlaneAtStart = true;

  Acceleration = 3000.f;
  Deceleration = 4000.f;
	MaxSpeed = MaxGroundSpeed;
}

void USAFInfantryMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {
	if (!PawnOwner || !UpdatedComponent || ShouldSkipUpdate(DeltaTime)) {
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
		return;
	}

	const FVector InputWorld = ConsumeWorldInput();
	FVector Accel = ComputeAcceleration(InputWorld, DeltaTime);
	if (bEnableSeparation)  ApplySeparation(Accel, DeltaTime);
	FVector NewVel = Velocity + Accel * DeltaTime;

	const bool bHasInput = !InputWorld.IsNearlyZero(1e-3f);
	if (!bHasInput) {
		const float Fric = FMath::Max(BrakingFriction, 0.f);
		const float Drag = Fric * DeltaTime;
		NewVel *= 1.f / (1.f + Drag);
	}

	if (bLockToXY) NewVel.Z = 0.f;

	const float MaxSpd = FMath::Max(MaxGroundSpeed, 1.f);
	const float NewSpeed = NewVel.Size();
	if (NewSpeed > MaxSpd) {
		NewVel *= (MaxSpd / NewSpeed);
	}

	const FVector Delta = NewVel * DeltaTime;

	FHitResult Hit;
	SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.IsValidBlockingHit()) {
		SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit);
	}

	Velocity = (UpdatedComponent->GetComponentLocation() - PawnOwner->GetActorLocation()) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
	if (bLockToXY) Velocity.Z = 0.f;
	ApplyRotation(DeltaTime);
	UpdateComponentVelocity();
}

FVector USAFInfantryMovementComponent::ConsumeWorldInput() {
	FVector Input = GetPendingInputVector();
	ConsumeInputVector();
	if (bLockToXY) Input.Z = 0.f;
	return Input;
}

FVector USAFInfantryMovementComponent::ComputeAcceleration(const FVector& InputWorld, float /*DeltaTime*/) const {
	FVector Out = FVector::ZeroVector;

	if (!InputWorld.IsNearlyZero(1e-3f)) {
		Out = InputWorld.GetSafeNormal() * Acceleration;
	} else if (!Velocity.IsNearlyZero(1e-3f)) {
		Out = -Velocity.GetSafeNormal() * Deceleration;

		const float StopTime = Velocity.Size() / FMath::Max(Deceleration, 1.f);
		if (StopTime <= GetWorld()->DeltaTimeSeconds) {
			Out = -Velocity / GetWorld()->DeltaTimeSeconds;
		}
	}

	if (bLockToXY) Out.Z = 0.f;
	return Out;
}

void USAFInfantryMovementComponent::ApplySeparation(FVector& OutAccel, float DeltaTime) {
	if (!UpdatedComponent) return;

	UWorld* World = GetWorld();
	if (!World || SeparationRadius <= 0.f || SeparationStrength <= 0.f) return;

	const FVector MyLoc = UpdatedComponent->GetComponentLocation();

	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(SeparationChannel);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SAFInfantrySeparation), false, PawnOwner);
	QueryParams.bTraceComplex = false;
	QueryParams.AddIgnoredActor(PawnOwner);
	QueryParams.AddIgnoredComponent(Cast<UPrimitiveComponent>(UpdatedComponent));

	const FCollisionShape Sphere = FCollisionShape::MakeSphere(SeparationRadius);

	TArray<FOverlapResult> Hits;
	World->OverlapMultiByObjectType(Hits, MyLoc, FQuat::Identity, ObjParams, Sphere, QueryParams);

	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;

	for (const FOverlapResult& Res : Hits) {
		const UPrimitiveComponent* OtherComp = Res.Component.Get();
		const AActor* OtherActor = Res.GetActor();
		if (!OtherComp || !OtherActor || OtherActor == PawnOwner) continue;

		const FVector Dir = MyLoc - OtherComp->GetComponentLocation();
		const float DistSq = FMath::Max(Dir.SizeSquared(), 1.f);
		Sum += Dir * FMath::InvSqrt(DistSq);
		++Count;
	}

	if (Count > 0) {
		FVector PushDir = Sum / float(Count);
		if (bLockToXY) PushDir.Z = 0.f;

		FVector SepAccel = PushDir.GetSafeNormal() * SeparationStrength;

		const float MaxVelDelta = SeparationMaxPush;
		const FVector SepVelDelta = SepAccel * DeltaTime;
		if (SepVelDelta.Size() > MaxVelDelta) {
			SepAccel = SepVelDelta.GetSafeNormal() * (MaxVelDelta / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
		}

		OutAccel += SepAccel;
	}
}

void USAFInfantryMovementComponent::ApplyRotation(float DeltaTime) {
	if (!PawnOwner) return;

	FRotator Current = PawnOwner->GetActorRotation();
	FRotator Target = Current;

	if (bUseDesiredFacing) {
		Target = FRotator(0.f, DesiredFacingYaw, 0.f);
	} else if (!bAllowStrafe) {
		if (!Velocity.IsNearlyZero(1e-2f)) {
			const FVector Dir = FVector(Velocity.X, Velocity.Y, 0.f).GetSafeNormal();
			Target = Dir.Rotation();
		}
	} else {
		Target = FRotator(0.f, Current.Yaw, 0.f);
	}

	const float Step = AimRotationRateDeg * DeltaTime;
	const float NewYaw = FMath::FixedTurn(Current.Yaw, Target.Yaw, Step);
	PawnOwner->SetActorRotation(FRotator(0.f, NewYaw, 0.f));
}
