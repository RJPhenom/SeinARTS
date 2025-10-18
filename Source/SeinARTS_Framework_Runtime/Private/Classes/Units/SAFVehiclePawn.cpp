#include "Classes/Units/SAFVehiclePawn.h"
#include "Classes/Units/SAFVehicle.h" 
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SAFMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Assets/Units/SAFVehicleAsset.h"
#include "Net/UnrealNetwork.h"
#include "Utils/SAFLibrary.h"
#include "DetourCrowdAIController.h"
#include "DrawDebugHelpers.h"
#include "Debug/SAFDebugTool.h"


ASAFVehiclePawn::ASAFVehiclePawn() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ADetourCrowdAIController::StaticClass();
	
	// Vehicle capsule (pathing collision)
	VehicleCapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Vehicle Capsule Component"));
	VehicleCapsuleComponent->InitCapsuleSize(42.f, 96.f);
	RootComponent = VehicleCapsuleComponent;

	// Vehicle mesh
	VehicleMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Vehicle Mesh Component"));
	VehicleMeshComponent->SetupAttachment(RootComponent);
	VehicleMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	VehicleMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Vehicle movement
	VehicleMovementComponent = CreateDefaultSubobject<USAFMovementComponent>(TEXT("Vehicle Movement Component"));
	VehicleMovementComponent->MovementMode = ESAFMovementMode::Tracked;
	VehicleMovementComponent->UpdatedComponent = RootComponent;
	SyncNavAgentWithCapsule();
}

void ASAFVehiclePawn::BeginPlay() {
	Super::BeginPlay();
}

void ASAFVehiclePawn::PostInitializeComponents() {
	Super::PostInitializeComponents();
	if(!ISAFActorInterface::Execute_GetAsset(this)) return;
	ApplyVisuals(); 
	SyncNavAgentWithCapsule();
}

// Asset Interface / API
// ===============================================================================================================================
/** Empty initializer prevents manual inits on this pawn class. 
 * (Initialization is handled by the owning vehicle class). */
void ASAFVehiclePawn::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {}

/** Handles asset getter by forwarding to the managing vehicle unit, if any */
USAFAsset* ASAFVehiclePawn::GetAsset_Implementation() const {
	ASAFVehicle* MyVehicle = ISAFVehiclePawnInterface::Execute_GetVehicle(this);
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(MyVehicle)) return ISAFActorInterface::Execute_GetAsset(MyVehicle);
	else SAFDEBUG_WARNING(FORMATSTR("GetAsset failed on VehiclePawn '%s': Vehicle is invalid.", *GetName()));
	return nullptr;
}

/** Returns true if this unit is able to be selected alongside other units 
 * (as opposed to click-select only). */
void ASAFVehiclePawn::SetMultiSelectable_Implementation(bool bNewMultiSelectable) {
	ASAFVehicle* MyVehicle = ISAFVehiclePawnInterface::Execute_GetVehicle(this);
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(MyVehicle)) return ISAFActorInterface::Execute_SetMultiSelectable(MyVehicle, bNewMultiSelectable);
	else SAFDEBUG_WARNING(FORMATSTR("SetMultiSelectable failed on VehiclePawn '%s': Vehicle is invalid.", *GetName()));
}

/** Handles selection by forwarding to the managing vehicle unit, if any */
bool ASAFVehiclePawn::Select_Implementation(AActor*& OutSelectedActor) {
	ASAFVehicle* MyVehicle = ISAFVehiclePawnInterface::Execute_GetVehicle(this);
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(MyVehicle)) return ISAFActorInterface::Execute_Select(MyVehicle, OutSelectedActor);
	else SAFDEBUG_ERROR(FORMATSTR("Select failed on VehiclePawn '%s': Vehicle is invalid.", *GetName()));
	return false;
}

/** Handles queue selection by forwarding to the managing vehicle unit, if any */
bool ASAFVehiclePawn::QueueSelect_Implementation(AActor*& OutQueueSelectedActor) {
	ASAFVehicle* MyVehicle = ISAFVehiclePawnInterface::Execute_GetVehicle(this);
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(MyVehicle)) return ISAFActorInterface::Execute_QueueSelect(MyVehicle, OutQueueSelectedActor);
	else SAFDEBUG_ERROR(FORMATSTR("QueueSelect failed on VehiclePawn '%s': Vehicle is invalid.", *GetName()));
	return false;
}

/** Handles deselection by forwarding to the managing vehicle unit, if any */
void ASAFVehiclePawn::Deselect_Implementation() {
	ASAFVehicle* MyVehicle = ISAFVehiclePawnInterface::Execute_GetVehicle(this);
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(MyVehicle)) ISAFActorInterface::Execute_Deselect(MyVehicle);
	else SAFDEBUG_WARNING(FORMATSTR("Deselect failed on VehiclePawn '%s': Vehicle is invalid or of wrong type.", *GetName()));
}

/** Handles dequeue selection by forwarding to the managing vehicle unit, if any */
void ASAFVehiclePawn::DequeueSelect_Implementation() {
	ASAFVehicle* MyVehicle = ISAFVehiclePawnInterface::Execute_GetVehicle(this);;
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(MyVehicle)) ISAFActorInterface::Execute_DequeueSelect(MyVehicle);
	else SAFDEBUG_WARNING(FORMATSTR("DeQueueSelect failed on VehiclePawn '%s': Vehicle is invalid or of wrong type.", *GetName()));
}

/** Handles OwningPlayer getter by forwarding to the managing vehicle unit, if any */
ASAFPlayerState* ASAFVehiclePawn::GetOwningPlayer_Implementation() const {
	ASAFVehicle* MyVehicle = ISAFVehiclePawnInterface::Execute_GetVehicle(this);
	if (SAFLibrary::IsActorPtrValidSeinARTSActor(MyVehicle)) return ISAFActorInterface::Execute_GetOwningPlayer(MyVehicle);
	else { SAFDEBUG_ERROR(FORMATSTR("GetOwningPlayer failed on VehiclePawn '%s': Vehicle is nullptr.", *GetName())); return nullptr; }
}

// Vehicle Pawn Interface / API
// ==================================================================================================
void ASAFVehiclePawn::InitVehiclePawn_Implementation(USAFVehicleAsset* InAsset, ASAFVehicle* InVehicle) {
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(InVehicle)) ISAFVehiclePawnInterface::Execute_SetVehicle(this, InVehicle);
	ApplyVisuals();
}

USAFVehicleAsset* ASAFVehiclePawn::GetVehicleAsset_Implementation() const {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Vehicle)) { SAFDEBUG_ERROR("GetVehicleAsset hit null Vehicle refernce on a vehicle pawn."); return nullptr; }
	USAFVehicleAsset* VehicleAsset = Cast<USAFVehicleAsset>(ISAFActorInterface::Execute_GetAsset(Vehicle));
	if (!VehicleAsset) SAFDEBUG_ERROR("GetVehicleAsset: Vehilce has invalid data type.");
	return VehicleAsset;
}

void ASAFVehiclePawn::SetVehicle_Implementation(ASAFVehicle* InVehicle) {
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(InVehicle)) { SAFDEBUG_WARNING("SetVehicle aborted: invalid actor."); return; }
	Vehicle = InVehicle;
}

// Internals
// ===================================================================================================================================================
/** Applies the visuals from VehicleAsset to this pawn */
void ASAFVehiclePawn::ApplyVisuals() {
	USAFVehicleAsset* VehicleAsset = ISAFVehiclePawnInterface::Execute_GetVehicleAsset(this);
	if (!VehicleAsset) SAFDEBUG_ERROR("ApplyVisuals aborted: vehilce has invalid data type.");

	// Setup SKM for VehiclePawn
	if (VehicleMeshComponent.Get()) {
		if (VehicleAsset->SkeletalMesh.IsValid() || VehicleAsset->SkeletalMesh.ToSoftObjectPath().IsValid()) {
			if (USkeletalMesh* SkeletalMesh = VehicleAsset->SkeletalMesh.LoadSynchronous()) VehicleMeshComponent->SetSkeletalMesh(SkeletalMesh);
			else SAFDEBUG_ERROR(FORMATSTR("Error loading skeletal mesh on VehiclePawn '%s'.", *GetName()));
		} else SAFDEBUG_ERROR(FORMATSTR("Invalid VehicleMesh on VehiclePawn '%s'.", *GetName()));

		UClass* AnimBP = VehicleAsset->AnimClass.Get();
		if (!AnimBP) AnimBP = VehicleAsset->AnimClass.LoadSynchronous();
		if (AnimBP && AnimBP->IsChildOf(UAnimInstance::StaticClass())) {
			VehicleMeshComponent->SetAnimInstanceClass(VehicleAsset->AnimClass.Get());
		} else SAFDEBUG_ERROR(FORMATSTR("Invalid AnimClass on VehicleAsset for VehiclePawn '%s'.", *GetName()));
	} 
	
	else SAFDEBUG_ERROR("ApplyVisuals: error getting SkeletalMeshComponent.");
}

/** Syncs the nav agent properties with the capsule size (called on init and on capsule size change). */
void ASAFVehiclePawn::SyncNavAgentWithCapsule() {
	if (!VehicleCapsuleComponent || !VehicleMovementComponent) { SAFDEBUG_WARNING("SyncNavAgentWithCapsule aborted: missing components."); return; }

	const float Radius = VehicleCapsuleComponent->GetScaledCapsuleRadius();
	const float Height = VehicleCapsuleComponent->GetScaledCapsuleHalfHeight() * 2.f;
	FNavAgentProperties& Props = const_cast<FNavAgentProperties&>(VehicleMovementComponent->GetNavAgentPropertiesRef());
	Props.AgentRadius = Radius;
	Props.AgentHeight = Height;
}

// Replication
// ==================================================================================================
void ASAFVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFVehiclePawn, Vehicle);
}

void ASAFVehiclePawn::OnRep_Vehicle() {
	SAFDEBUG_INFO(TEXT("OnRep_Vehicle triggered."));
	ApplyVisuals();
}
