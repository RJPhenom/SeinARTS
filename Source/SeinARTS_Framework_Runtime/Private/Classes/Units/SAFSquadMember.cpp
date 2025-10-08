#include "Classes/Units/SAFSquadMember.h"
#include "Classes/Units/SAFSquad.h" 
#include "Classes/Unreal/SAFPlayerState.h"
#include "Assets/Units/SAFSquadMemberAsset.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SAFInfantryMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "AIController.h"
#include "DetourCrowdAIController.h"
#include "Utils/SAFLibrary.h"
#include "DrawDebugHelpers.h"
#include "Debug/SAFDebugTool.h"

ASAFSquadMember::ASAFSquadMember() {
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; 
	SetReplicateMovement(true);

	// // Root collision
	// InfantryCapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	// InfantryCapsuleComponent->InitCapsuleSize(42.f, 96.f);
	// InfantryCapsuleComponent->SetCollisionProfileName(TEXT("Pawn"));
	// RootComponent = InfantryCapsuleComponent;

	// // Mesh
	// InfantryMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	// InfantryMeshComponent->SetupAttachment(InfantryCapsuleComponent);
	// InfantryMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// InfantryMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Movement (UPawnMovementComponent)
	// InfantryMovementComponent = CreateDefaultSubobject<USAFInfantryMovementComponent>(TEXT("InfantryMovementComponent"));
	// InfantryMovementComponent->UpdatedComponent = InfantryCapsuleComponent;

	// AI
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ADetourCrowdAIController::StaticClass();
}

void ASAFSquadMember::BeginPlay() {
	Super::BeginPlay();
	OnRep_SquadMemberAsset();
	// Apply designer mesh offset if present (no character smoothing tricks needed)
	if (IsValid(InfantryMeshComponent) && IsValid(SquadMemberAsset)) 
		InfantryMeshComponent->SetRelativeTransform(SquadMemberAsset->CharacterMeshOffset);
}

void ASAFSquadMember::PostInitializeComponents() {
	Super::PostInitializeComponents();
	if (!SquadMemberAsset) return;
	ApplyVisuals();
}

// Asset Interface / API
// ==================================================================================================
void ASAFSquadMember::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {}

USAFAsset* ASAFSquadMember::GetAsset_Implementation() const {
	ASAFSquad* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(this);
	if (IsValid(MySquad)) return ISAFActorInterface::Execute_GetAsset(MySquad);
	else SAFDEBUG_ERROR(FORMATSTR("GetAsset failed on SquadMember '%s': Squad is invalid.", *GetName()));
	return nullptr;
}

ASAFPlayerState* ASAFSquadMember::GetOwningPlayer_Implementation() const {
	ASAFSquad* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(this);
	if (IsValid(MySquad)) return Cast<ASAFPlayerState>(ISAFActorInterface::Execute_GetOwningPlayer(MySquad));
	else { SAFDEBUG_ERROR(FORMATSTR("GetOwningPlayer failed on SquadMember '%s': Squad is nullptr.", *GetName())); return nullptr; }
}

bool ASAFSquadMember::GetMultiSelectable_Implementation() const {
	ASAFSquad* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(const_cast<ASAFSquadMember*>(this));
	return IsValid(MySquad) ? ISAFActorInterface::Execute_GetMultiSelectable(MySquad) : false;
}

void ASAFSquadMember::SetMultiSelectable_Implementation(bool bNewMultiSelectable) {
	ASAFSquad* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(this);
	if (IsValid(MySquad)) ISAFActorInterface::Execute_SetMultiSelectable(MySquad, bNewMultiSelectable);
	else SAFDEBUG_WARNING(FORMATSTR("SetMultiSelectable failed on SquadMember '%s': Squad is invalid.", *GetName()));
}

bool ASAFSquadMember::Select_Implementation(AActor*& OutSelectedActor) {
	AActor* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(this);
	if (IsValid(MySquad)) return ISAFActorInterface::Execute_Select(MySquad, OutSelectedActor);
	else SAFDEBUG_ERROR(FORMATSTR("Select failed on SquadMember '%s': Squad is invalid.", *GetName()));
	return false;
}

bool ASAFSquadMember::QueueSelect_Implementation(AActor*& OutQueueSelectedActor) {
	AActor* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(this);  
	if (IsValid(MySquad)) return ISAFActorInterface::Execute_QueueSelect(MySquad, OutQueueSelectedActor);
	else SAFDEBUG_ERROR(FORMATSTR("QueueSelect failed on SquadMember '%s': Squad is invalid.", *GetName()));
	return false;
}

void ASAFSquadMember::Deselect_Implementation() {
	AActor* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(this);
	if (IsValid(MySquad)) ISAFActorInterface::Execute_Deselect(MySquad);
	else SAFDEBUG_WARNING(FORMATSTR("Deselect failed on SquadMember '%s': Squad is invalid or wrong type.", *GetName()));
}

void ASAFSquadMember::DequeueSelect_Implementation() {
	AActor* MySquad = ISAFSquadMemberInterface::Execute_GetSquad(this);
	if (IsValid(MySquad)) ISAFActorInterface::Execute_DequeueSelect(MySquad);
	else SAFDEBUG_WARNING(FORMATSTR("DequeueSelect failed on SquadMember '%s': Squad is invalid or wrong type.", *GetName()));
}

// Cover Interface / API
// ==================================================================================================
void ASAFSquadMember::EnterCover_Implementation(AActor* CoverObject, ESAFCoverType CoverType) {
	if (IsValid(CoverObject)) CoverActors.AddUnique(CoverObject);
	FColor DebugColor = FColor::Silver;
	switch (CoverType) {
		case ESAFCoverType::Heavy:    DebugColor = FColor::Green;  break;
		case ESAFCoverType::Light:    DebugColor = FColor::Yellow; break;
		case ESAFCoverType::Negative: DebugColor = FColor::Red;    break;
		case ESAFCoverType::Neutral:  DebugColor = FColor::Silver; break;
		default: break;
	} DrawDebugString(GetWorld(),	GetActorLocation() + FVector(0.f, 0.f, 15.f),	TEXT("Entered cover!"),	nullptr, DebugColor,	5.0f,	true);
}

void ASAFSquadMember::ExitCover_Implementation(AActor* CoverObject, ESAFCoverType CoverType) {
	const bool bRemoved = CoverActors.Remove(CoverObject) > 0; (void)bRemoved;
	FColor DebugColor = FColor::Silver;
	switch (CoverType) {
		case ESAFCoverType::Heavy:    DebugColor = FColor::Green;  break;
		case ESAFCoverType::Light:    DebugColor = FColor::Yellow; break;
		case ESAFCoverType::Negative: DebugColor = FColor::Red;    break;
		case ESAFCoverType::Neutral:  DebugColor = FColor::Silver; break;
		default: break;
	} DrawDebugString(GetWorld(),	GetActorLocation() + FVector(0.f, 0.f, 15.f),	TEXT("Exited cover!"),	nullptr, DebugColor,	5.0f,	true);
}

// Squad Member Interface / API
// ==================================================================================================
void ASAFSquadMember::InitSquadMember_Implementation(USAFSquadMemberAsset* InAsset, ASAFSquad* InSquad) {
	if (!HasAuthority()) return;
	if (!IsValid(InAsset) || !IsValid(InSquad)) { SAFDEBUG_ERROR(FORMATSTR("SquadMember '%s' received null asset or squad on initialization. Destroying.", *GetName())); Destroy(); return; }

	SquadMemberAsset = InAsset;
	ISAFSquadMemberInterface::Execute_SetSquad(this, InSquad);
	if (!ISAFSquadMemberInterface::Execute_HasSquad(this)) { SAFDEBUG_ERROR(FORMATSTR("SquadMember '%s' has no squad after initialization. Destroying.", *GetName())); Destroy();  return; }

	OnRep_SquadMemberAsset();
}

void ASAFSquadMember::SetSquad_Implementation(ASAFSquad* InSquad) {
	if (!HasAuthority()) return;
	if (IsValid(InSquad)) {
		Squad = InSquad;
		OnRep_Squad();
		SAFDEBUG_SUCCESS(FORMATSTR("SetSquad server-side: '%s' -> '%s'.", *GetName(), *InSquad->GetName()));
	} else {
		Squad = nullptr;
		SAFDEBUG_WARNING("SetSquad server-side: InSquad invalid. SquadMember will be orphaned!");
	}
}

// Internals
// ==================================================================================================
void ASAFSquadMember::ApplyVisuals() {
	if (!SquadMemberAsset) return;

	if (USkeletalMeshComponent* MeshComp = GetMesh()) {
		// Mesh
		if (SquadMemberAsset->SkeletalMesh.IsValid() || SquadMemberAsset->SkeletalMesh.ToSoftObjectPath().IsValid()) {
			if (USkeletalMesh* SkeletalMesh = SquadMemberAsset->SkeletalMesh.LoadSynchronous()) MeshComp->SetSkeletalMesh(SkeletalMesh);
			else SAFDEBUG_ERROR(FORMATSTR("Error loading skeletal mesh on SquadMember '%s'.", *GetName()));
		} else SAFDEBUG_ERROR(FORMATSTR("Invalid SkeletalMesh on SquadMemberAsset for '%s'.", *GetName()));

		// Anim BP / Class
		if (SquadMemberAsset->AnimClass) MeshComp->SetAnimInstanceClass(SquadMemberAsset->AnimClass);
		else SAFDEBUG_ERROR(FORMATSTR("Invalid AnimClass on SquadMemberAsset for '%s'.", *GetName()));

		// Designer offset
		MeshComp->SetRelativeTransform(SquadMemberAsset->CharacterMeshOffset);
	} 
	
	else SAFDEBUG_ERROR("ApplyVisuals: error getting SkeletalMeshComponent.");
}

// Replication
// ==================================================================================================
void ASAFSquadMember::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFSquadMember, Squad);
	DOREPLIFETIME(ASAFSquadMember, SquadMemberAsset);
}

void ASAFSquadMember::OnRep_SquadMemberAsset() { 
	if (!SquadMemberAsset) return;
	ApplyVisuals();
}

void ASAFSquadMember::OnRep_Squad() { 
	OnSquadChanged.Broadcast(Squad.Get());
}
