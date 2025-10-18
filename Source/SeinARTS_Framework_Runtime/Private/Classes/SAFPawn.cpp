#include "Classes/SAFPawn.h"
#include "Classes/SAFUnit.h"
#include "Classes/SAFPlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SAFMovementComponent.h"
#include "Assets/SAFPawnAsset.h"
#include "Engine/ActorChannel.h"
#include "Engine/Engine.h"
#include "DetourCrowdAIController.h"
#include "Net/UnrealNetwork.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Debug/SAFDebugTool.h"

ASAFPawn::ASAFPawn(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
	// Tick
	PrimaryActorTick.bCanEverTick = false;

	// Replication
	bReplicates = true;
	SetReplicateMovement(true);

	// AI
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ADetourCrowdAIController::StaticClass();

	// Capsule
	PawnCapsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Pawn Capsule"));
	PawnCapsule->InitCapsuleSize(50.f, 100.f);
	RootComponent = PawnCapsule;

	// Mesh
	PawnMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Pawn Mesh"));
	PawnMesh->SetupAttachment(PawnCapsule);
	PawnMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PawnMesh->SetCollisionObjectType(ECollisionChannel::ECC_Pawn);
	PawnMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Movement
	PawnMovement = CreateDefaultSubobject<USAFMovementComponent>(TEXT("Pawn Movement"));
	PawnMovement->SetUpdatedComponent(PawnCapsule);
}

void ASAFPawn::PostInitializeComponents() {
	Super::PostInitializeComponents();
	if(!ISAFPawnInterface::Execute_GetPawnAsset(this)) return;

	// Reapply configurations
	ApplyCapsuleConfiguration();
	ApplyVisualConfiguration();
	ApplyMovementConfiguration();
}

// Actor Interface Implementation
// =================================================================================================================================================
/** Empty on purpose to skip parent init from running (pawns own their own initialization) */
void ASAFPawn::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {}

USAFAsset* ASAFPawn::GetAsset_Implementation() const {
	ASAFUnit* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	if (IsValid(MyUnit)) return ISAFActorInterface::Execute_GetAsset(MyUnit);
	else SAFDEBUG_ERROR(FORMATSTR("GetAsset failed on Pawn '%s': Unit is invalid.", *GetName()));
	return nullptr;
}

ASAFPlayerState* ASAFPawn::GetOwningPlayer_Implementation() const {
	ASAFUnit* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	if (IsValid(MyUnit)) return Cast<ASAFPlayerState>(ISAFActorInterface::Execute_GetOwningPlayer(MyUnit));
	else { SAFDEBUG_ERROR(FORMATSTR("GetOwningPlayer failed on Pawn '%s': Unit is nullptr.", *GetName())); return nullptr; }
}

bool ASAFPawn::GetMultiSelectable_Implementation() const {
	ASAFUnit* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	return IsValid(MyUnit) ? ISAFActorInterface::Execute_GetMultiSelectable(MyUnit) : false;
}

void ASAFPawn::SetMultiSelectable_Implementation(bool bNewMultiSelectable) {
	ASAFUnit* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	if (IsValid(MyUnit)) ISAFActorInterface::Execute_SetMultiSelectable(MyUnit, bNewMultiSelectable);
	else SAFDEBUG_WARNING(FORMATSTR("SetMultiSelectable failed on Pawn '%s': Unit is invalid.", *GetName()));
}

bool ASAFPawn::Select_Implementation(AActor*& OutSelectedActor) {
	AActor* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	if (IsValid(MyUnit)) return ISAFActorInterface::Execute_Select(MyUnit, OutSelectedActor);
	else SAFDEBUG_ERROR(FORMATSTR("Select failed on Pawn '%s': Unit is invalid.", *GetName()));
	return false;
}

bool ASAFPawn::QueueSelect_Implementation(AActor*& OutQueueSelectedActor) {
	AActor* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	if (IsValid(MyUnit)) return ISAFActorInterface::Execute_QueueSelect(MyUnit, OutQueueSelectedActor);
	else SAFDEBUG_ERROR(FORMATSTR("QueueSelect failed on Pawn '%s': Unit is invalid.", *GetName()));
	return false;
}

void ASAFPawn::Deselect_Implementation() {
	AActor* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	if (IsValid(MyUnit)) ISAFActorInterface::Execute_Deselect(MyUnit);
	else SAFDEBUG_WARNING(FORMATSTR("Deselect failed on Pawn '%s': Unit is invalid or wrong type.", *GetName()));
}

void ASAFPawn::DequeueSelect_Implementation() {
	AActor* MyUnit = ISAFPawnInterface::Execute_GetOwningUnit(this);
	if (IsValid(MyUnit)) ISAFActorInterface::Execute_DequeueSelect(MyUnit);
	else SAFDEBUG_WARNING(FORMATSTR("DequeueSelect failed on Pawn '%s': Unit is invalid or wrong type.", *GetName()));
}

// Pawn Interface Implementation
// =================================================================================================================================================
void ASAFPawn::InitPawn_Implementation(USAFPawnAsset* InPawnAsset, ASAFUnit* InOwningUnit) {
	if (!InPawnAsset) { SAFDEBUG_ERROR(FORMATSTR("InitPawn_Implementation: Null PawnAsset passed to pawn '%s'", *GetName())); return; }
	if (!InOwningUnit) { SAFDEBUG_ERROR(FORMATSTR("InitPawn_Implementation: Null OwningUnit passed to pawn '%s'", *GetName())); return; }

	SAFDEBUG_INFO(FORMATSTR(
		"InitPawn_Implementation: Initializing pawn '%s' with asset '%s' for unit '%s'", 
		*GetName(), *InPawnAsset->GetName(), *InOwningUnit->GetName()
	));
	
	PawnAsset = InPawnAsset;
	OwningUnit = InOwningUnit;
	
	// Configure components
	ApplyCapsuleConfiguration();
	ApplyVisualConfiguration();
	ApplyMovementConfiguration();
}

// Helpers / Internals
// =================================================================================================================================================
/** Gets the current velocity of the pawn. */
FVector ASAFPawn::GetPawnVelocity() const {
	if (PawnMovement) return PawnMovement->Velocity;
	return FVector::ZeroVector;
}

/** Apply collision configuration from the pawn asset. */
void ASAFPawn::ApplyCapsuleConfiguration() {
	if (!PawnAsset || !PawnCapsule) return;
	PawnCapsule->SetCapsuleRadius(PawnAsset->CapsuleRadius);
	PawnCapsule->SetCapsuleHalfHeight(PawnAsset->CapsuleHalfHeight);
	SetRootComponent(PawnCapsule);
	SyncNavAgentWithCapsule();
}

/** Apply visual configuration from the pawn asset. */
void ASAFPawn::ApplyVisualConfiguration() {
	if (!PawnAsset || !PawnMesh) return;

	// Apply skeletal mesh
	if (!PawnAsset->SkeletalMesh.IsNull()) {
		if (USkeletalMesh* LoadedMesh = PawnAsset->SkeletalMesh.LoadSynchronous()) PawnMesh->SetSkeletalMesh(LoadedMesh);
		else SAFDEBUG_ERROR(FORMATSTR("Error loading skeletal mesh on Pawn '%s'.", *GetName()));
	} else SAFDEBUG_ERROR(FORMATSTR("Invalid SkeletalMesh on Pawn '%s'.", *GetName()));

	// Apply animation blueprint
	if (!PawnAsset->AnimClass.IsNull()) {
		if (UClass* LoadedAnimClass = PawnAsset->AnimClass.LoadSynchronous()) PawnMesh->SetAnimInstanceClass(LoadedAnimClass);
		else SAFDEBUG_ERROR(FORMATSTR("Error loading animation class on Pawn '%s'.", *GetName()));
	} else SAFDEBUG_ERROR(FORMATSTR("Invalid AnimClass on Pawn '%s'.", *GetName()));

	// Apply transform offset
	PawnMesh->SetRelativeTransform(PawnAsset->MeshOffset);
}

/** Apply movement configuration to the movement component 
 * (creates a new movement component since the movement component class is part of the configuration). */
void ASAFPawn::ApplyMovementConfiguration() {
	if (!PawnAsset || !PawnMovement) return;

	// Core movement properties
	PawnMovement->MovementMode          = PawnAsset->MovementMode;
	PawnMovement->MaxSpeed              = PawnAsset->MaxSpeed;
	PawnMovement->Acceleration          = PawnAsset->Acceleration;
	PawnMovement->Deceleration          = PawnAsset->Deceleration;
	PawnMovement->TurningBoost          = PawnAsset->TurningBoost;
	PawnMovement->bConstrainToPlane     = PawnAsset->bConstrainToPlane;
	PawnMovement->bSnapToPlaneAtStart   = PawnAsset->bSnapToPlaneAtStart;
	PawnMovement->SetPlaneConstraintNormal(PawnAsset->PlaneConstraintNormal);
	PawnMovement->SetMaxRotationRate(PawnAsset->MaxRotationRate);

	// Navigation properties
	PawnMovement->bProjectToNavMesh = PawnAsset->bProjectToNavMesh;
	PawnMovement->NavProjectionExtent = PawnAsset->NavProjectionExtent;
	PawnMovement->StopSpeedThreshold = PawnAsset->StopSpeedThreshold;

	// Infantry-specific properties
	PawnMovement->Infantry_bAllowStrafe = PawnAsset->Infantry_bAllowStrafe;
	PawnMovement->Infantry_bUseDesiredFacing = PawnAsset->Infantry_bUseDesiredFacing;
	PawnMovement->Infantry_DesiredFacingYaw = PawnAsset->Infantry_DesiredFacingYaw;
	PawnMovement->Infantry_FacingRotationRate = PawnAsset->Infantry_FacingRotationRate;

	// Tracked vehicle properties
	PawnMovement->Tracked_MaxTurnRateDeg = PawnAsset->Tracked_MaxTurnRateDeg;
	PawnMovement->Tracked_ReverseEngageDotThreshold = PawnAsset->Tracked_ReverseEngageDotThreshold;
	PawnMovement->Tracked_ReverseEngageDistanceThreshold = PawnAsset->Tracked_ReverseEngageDistanceThreshold;
	PawnMovement->Tracked_ReverseMaxSpeed = PawnAsset->Tracked_ReverseMaxSpeed;
	PawnMovement->Tracked_ThrottleVsMisalignmentDeg = PawnAsset->Tracked_ThrottleVsMisalignmentDeg;

	// Wheeled vehicle properties
	PawnMovement->Wheeled_MaxTurnRateDeg = PawnAsset->Wheeled_MaxTurnRateDeg;
	PawnMovement->Wheeled_ReverseEngageDotThreshold = PawnAsset->Wheeled_ReverseEngageDotThreshold;
	PawnMovement->Wheeled_ReverseEngageDistanceThreshold = PawnAsset->Wheeled_ReverseEngageDistanceThreshold;
	PawnMovement->Wheeled_ReverseMaxSpeed = PawnAsset->Wheeled_ReverseMaxSpeed;
	PawnMovement->Wheeled_Wheelbase = PawnAsset->Wheeled_Wheelbase;
	PawnMovement->Wheeled_MaxSteerAngleDeg = PawnAsset->Wheeled_MaxSteerAngleDeg;
	PawnMovement->Wheeled_SteerResponse = PawnAsset->Wheeled_SteerResponse;

	// Hover vehicle properties
	PawnMovement->Hover_MaxTurnRateDeg = PawnAsset->Hover_MaxTurnRateDeg;
	PawnMovement->Hover_ReverseEngageDotThreshold = PawnAsset->Hover_ReverseEngageDotThreshold;
	PawnMovement->Hover_ReverseEngageDistanceThreshold = PawnAsset->Hover_ReverseEngageDistanceThreshold;
	PawnMovement->Hover_ReverseMaxSpeed = PawnAsset->Hover_ReverseMaxSpeed;

	// Apply movement mode defaults to ensure proper base settings
	PawnMovement->ApplyMovementModeDefaults();
}

/** Sync the navigation agent's capsule size with the pawn's capsule component. */
void ASAFPawn::SyncNavAgentWithCapsule() {
	if (!PawnCapsule || !PawnMovement) return; 
	if (PawnMovement->UpdatedComponent != PawnCapsule)	{
		SAFDEBUG_INFO("PawnMovement's UpdatedComponent does not match PawnCapsule.");
		PawnMovement->SetUpdatedComponent(PawnCapsule);
	}

	const float Radius = PawnCapsule->GetScaledCapsuleRadius();
	const float Height = PawnCapsule->GetScaledCapsuleHalfHeight() * 2.f;

	FNavAgentProperties& Props = const_cast<FNavAgentProperties&>(PawnMovement->GetNavAgentPropertiesRef());
	Props.AgentRadius = Radius;
	Props.AgentHeight = Height;
}

// Replication
// ====================================================================================================================================================
void ASAFPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFPawn, PawnAsset);
	DOREPLIFETIME(ASAFPawn, OwningUnit);
}

void ASAFPawn::OnRep_OwningUnit() {
	ApplyCapsuleConfiguration();
	ApplyVisualConfiguration();
	ApplyMovementConfiguration();
}