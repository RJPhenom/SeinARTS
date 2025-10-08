#include "Classes/Units/SAFPawn.h"
#include "Assets/Units/SAFPawnAsset.h"
#include "Classes/Units/SAFUnit.h"
#include "Components/SAFMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Engine/Engine.h"
#include "Debug/SAFDebugTool.h"

ASAFPawn::ASAFPawn(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	// Create core components
	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("CapsuleComponent"));
	SetRootComponent(CapsuleComponent);

	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CapsuleComponent);

	// Set default collision settings
	CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CapsuleComponent->SetCollisionObjectType(ECollisionChannel::ECC_Pawn);
	CapsuleComponent->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Block);
	CapsuleComponent->SetCollisionResponseToChannel(ECollisionChannel::ECC_Camera, ECollisionResponse::ECR_Ignore);

	// Set default mesh settings
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCollisionObjectType(ECollisionChannel::ECC_Pawn);
	MeshComponent->SetCollisionResponseToAllChannels(ECollisionResponse::ECR_Ignore);

	// Set default capsule size (will be overridden by asset)
	CapsuleComponent->SetCapsuleRadius(50.0f);
	CapsuleComponent->SetCapsuleHalfHeight(100.0f);
}

void ASAFPawn::BeginPlay() {
	Super::BeginPlay();
	
	// If we have a pawn asset set, configure from it
    USAFPawnAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(PawnAsset);
	if (ResolvedAsset) ConfigureFromAsset(ResolvedAsset);
}

void ASAFPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) {
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// Input setup can be handled by the owning unit or game mode
}

UPawnMovementComponent* ASAFPawn::GetMovementComponent() const {
	return SAFMovementComponent;
}

void ASAFPawn::ConfigureFromAsset(USAFPawnAsset* InPawnAsset) {
	if (!InPawnAsset) { SAFDEBUG_WARNING("ASAFPawn::ConfigureFromAsset - PawnAsset is null"); return; }
	
    PawnAsset = InPawnAsset;

	// Apply configuration in order
	ApplyCollisionConfiguration();
	CreateMovementComponent();
	ApplyMovementConfiguration();
	ApplyVisualConfiguration();
}

void ASAFPawn::ApplyVisualConfiguration() {
	if (!PawnAsset || !MeshComponent) return;

	// Apply skeletal mesh
	if (!PawnAsset->SkeletalMesh.IsNull()) {
		if (USkeletalMesh* LoadedMesh = PawnAsset->SkeletalMesh.LoadSynchronous()) {
			MeshComponent->SetSkeletalMesh(LoadedMesh);
		}
	}

	// Apply animation blueprint
	if (!PawnAsset->AnimClass.IsNull()) {
		if (UClass* LoadedAnimClass = PawnAsset->AnimClass.LoadSynchronous()) {
			MeshComponent->SetAnimInstanceClass(LoadedAnimClass);
		}
	}

	// Apply mesh transform offset
	MeshComponent->SetRelativeTransform(PawnAsset->MeshOffset);
}

void ASAFPawn::ApplyCollisionConfiguration() {
	if (!PawnAsset || !CapsuleComponent) return;

	// Apply collision settings
	CapsuleComponent->SetCapsuleRadius(PawnAsset->CapsuleRadius);
	CapsuleComponent->SetCapsuleHalfHeight(PawnAsset->CapsuleHalfHeight);
}

void ASAFPawn::CreateMovementComponent() {
	if (!PawnAsset || !PawnAsset->MovementComponentClass) {
		UE_LOG(LogTemp, Warning, TEXT("ASAFPawn::CreateMovementComponent - PawnAsset or MovementComponentClass is null"));
		return;
	}

	// Destroy existing movement component if present
	if (SAFMovementComponent) {
		SAFMovementComponent->DestroyComponent();
		SAFMovementComponent = nullptr;
	}

	// Create new movement component of the specified type
	SAFMovementComponent = NewObject<USAFMovementComponent>(this, PawnAsset->MovementComponentClass, TEXT("SAFMovementComponent"));
	if (SAFMovementComponent) {
		SAFMovementComponent->SetUpdatedComponent(CapsuleComponent);
		SAFMovementComponent->RegisterComponent();
	}
}

void ASAFPawn::ApplyMovementConfiguration() {
	if (!PawnAsset || !SAFMovementComponent) return;

	// Apply movement settings
	SAFMovementComponent->MaxSpeed              = PawnAsset->MaxSpeed;
	SAFMovementComponent->Acceleration          = PawnAsset->Acceleration;
	SAFMovementComponent->Deceleration          = PawnAsset->Deceleration;
	SAFMovementComponent->TurningBoost          = PawnAsset->TurningBoost;
	SAFMovementComponent->bConstrainToPlane     = PawnAsset->bConstrainToPlane;
	SAFMovementComponent->bSnapToPlaneAtStart   = PawnAsset->bSnapToPlaneAtStart;
	SAFMovementComponent->SetPlaneConstraintNormal(PawnAsset->PlaneConstraintNormal);

	// Set custom max rotation rate (this is a custom property we added)
	if (PawnAsset->MaxRotationRate > 0.0f) {
		// Convert degrees per second to rotator per second
		FRotator MaxRotRate(0.0f, PawnAsset->MaxRotationRate, 0.0f);
		// Note: SAFMovementComponent should have a SetMaxRotationRate method
		// For now, we'll comment this out since it may not exist yet
		// SAFMovementComponent->SetMaxRotationRate(MaxRotRate);
	}
}

// ISAFPawnInterface Implementation
void ASAFPawn::InitPawn_Implementation(USAFPawnAsset* InPawnAsset, ASAFUnit* InOwningUnit) {
	PawnAsset = InPawnAsset;
	OwningUnit = InOwningUnit;
	ConfigureFromAsset(InPawnAsset);
}

void ASAFPawn::SetOwningUnit_Implementation(ASAFUnit* InOwningUnit) {
	OwningUnit = InOwningUnit;
}

FVector ASAFPawn::GetPawnVelocity() const {
	if (SAFMovementComponent) return SAFMovementComponent->Velocity;
	return FVector::ZeroVector;
}