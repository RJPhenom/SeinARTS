#include "Components/SAFActorComponent.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Resolvers/SAFOwnershipResolver.h"
#include "Debug/SAFDebugTool.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "Kismet/KismetSystemLibrary.h"

USAFActorComponent::USAFActorComponent() {
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

// Actor Interface Overrides
// =====================================================================================================================================
USAFAsset* USAFActorComponent::GetAsset_Implementation() const {
	return SAFAssetResolver::ResolveAsset(Asset);
}

void USAFActorComponent::SetAsset_Implementation(USAFAsset* InAsset) {
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (!InAsset) { Asset = nullptr; return; }
	Asset = InAsset;
}

void USAFActorComponent::InitAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner) {
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;

	// Resolve asset
	Asset = InAsset ? InAsset : SAFAssetResolver::ResolveAsset(Asset);
	if (!Asset.IsValid()) {
		SAFDEBUG_ERROR("USAFActorComponent::InitAsset failed: Asset is null.");
		return;
	}

	// Resolve owner
	if (InOwner) {
		OwningPlayer = InOwner;
	} else {
		ASAFPlayerState* ResolvedOwner = nullptr;
		if (!SAFOwnershipResolver::ResolveOwner(GetWorld(), InitTeamID, InitPlayerID, ResolvedOwner)) {
			SAFDEBUG_WARNING("USAFActorComponent::InitAsset: could not resolve owner; leaving unowned.");
		}
		OwningPlayer = ResolvedOwner;
	}

	// Hook for spawn lifecycle if you want parity with old flow
	OnSpawn.Broadcast();
}

void USAFActorComponent::SetOwningPlayer_Implementation(ASAFPlayerState* InOwner) {
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	OwningPlayer = InOwner;
}

FText USAFActorComponent::GetDisplayName_Implementation() const {
	if (const USAFAsset* Resolved = SAFAssetResolver::ResolveAsset(Asset))
		return Resolved->DisplayName;
	return FText::FromString(GetOwner() ? GetOwner()->GetName() : TEXT("Uninitialized SAF Object"));
}

UTexture2D* USAFActorComponent::GetIcon_Implementation() const {
	if (const USAFAsset* Resolved = SAFAssetResolver::ResolveAsset(Asset))
		return SAFAssetResolver::ResolveAsset(Resolved->Icon);
	return nullptr;
}

UTexture2D* USAFActorComponent::GetPortrait_Implementation() const {
	if (const USAFAsset* Resolved = SAFAssetResolver::ResolveAsset(Asset))
		return SAFAssetResolver::ResolveAsset(Resolved->Icon);
	return nullptr;
}

bool USAFActorComponent::Select_Implementation(AActor*& OutSelectedActor) {
	if (!bSelectable) return false;
	bIsSelected = true;
	OutSelectedActor = GetOwner();
	OnSelectionChanged.Broadcast(true);
	return true;
}

bool USAFActorComponent::QueueSelect_Implementation(AActor*& OutQueueSelectedActor) {
	if (!bSelectable) return false;
	bIsQueueSelected = true;
	OutQueueSelectedActor = GetOwner();
	OnQueueSelectionChanged.Broadcast(true);
	return true;
}

void USAFActorComponent::Deselect_Implementation() {
	if (!bIsSelected) return;
	bIsSelected = false;
	OnSelectionChanged.Broadcast(false);
}

void USAFActorComponent::DequeueSelect_Implementation() {
	if (!bIsQueueSelected) return;
	bIsQueueSelected = false;
	OnQueueSelectionChanged.Broadcast(false);
}

void USAFActorComponent::Place_Implementation(FVector Location, FRotator Rotation) {
	if (AActor* Host = GetOwner()) {
		if (Host->HasAuthority()) {
			Host->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		OnPlaced.Broadcast(Location, Rotation);
	}
}

// Helpers
// ==============================================================================================================
USAFActorComponent* USAFActorComponent::FindSeinARTSComponentOn(AActor* Actor) {
	return Actor ? Actor->FindComponentByClass<USAFActorComponent>() : nullptr;
}

// Replication
// ==============================================================================================================
void USAFActorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USAFActorComponent, Asset);
	DOREPLIFETIME(USAFActorComponent, OwningPlayer);
	DOREPLIFETIME(USAFActorComponent, bSelectable);
	DOREPLIFETIME(USAFActorComponent, bPingable);
	DOREPLIFETIME_CONDITION(USAFActorComponent, bIsSelected, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(USAFActorComponent, bIsQueueSelected, COND_OwnerOnly);
}

void USAFActorComponent::OnRep_OwningPlayer() {
	// Intentionally empty; hook if UI/FX need to react to owner changes.
}
