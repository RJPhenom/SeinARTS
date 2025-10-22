#include "Classes/SAFActor.h"
#include "Classes/SAFFormationManager.h"
#include "Classes/SAFPlayerState.h"
#include "Classes/SAFGameState.h"
#include "Components/SphereComponent.h"
#include "Assets/SAFAsset.h"
#include "Interfaces/SAFActorInterface.h"
#include "Resolvers/SAFOwnershipResolver.h"
#include "Net/UnrealNetwork.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"

#if WITH_EDITOR
#include "Components/StaticMeshComponent.h"
#endif

ASAFActor::ASAFActor() {

	// Selection probes on units.
	// The default engine 'GetActorsInSelectionRectanlge' is a garbage function,
	// eventually I will replace it with a respectable alternative that actually works.
	// Until then, we append a sphere component to fix bounds fallback issues causing 
	// abstract manager classes with no extents being selectable by world 0 because the
	// epic function has no validation on asbtract manager actors with no extents (there 
	// are only like several key actors of the game framework like that! But Im a retard 
	// and I code for epic!!)
	USphereComponent* SelectionProbe = CreateDefaultSubobject<USphereComponent>(TEXT("TinySelectionProbe"));
	SelectionProbe->SetupAttachment(RootComponent);
	SelectionProbe->InitSphereRadius(0.5f);
	SelectionProbe->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SelectionProbe->SetCollisionResponseToAllChannels(ECR_Ignore);
	SelectionProbe->SetComponentTickEnabled(false);
	SelectionProbe->SetGenerateOverlapEvents(false);
	SelectionProbe->SetGenerateOverlapEvents(false);
	SelectionProbe->SetShouldUpdatePhysicsVolume(false);
	SelectionProbe->SetCanEverAffectNavigation(false);
	SelectionProbe->SetVisibility(false, true);
	SelectionProbe->SetIsReplicated(false);
	SelectionProbe->PrimaryComponentTick.bCanEverTick = false;
	SelectionProbe->bUseAttachParentBound = false;  

}

void ASAFActor::PreInitializeComponents() {
	Super::PreInitializeComponents();
	
#if WITH_EDITOR
	// CRITICAL: Clean up editor preview components before runtime initialization
	// This is called BEFORE components initialize, so we can destroy editor components
	// before they affect gameplay
	if (GetWorld() && GetWorld()->IsGameWorld()) {
		ClearEditorPreview();
	}
#endif
}

// Actor Interface Overrides
// =======================================================================================================================
void ASAFActor::SetAsset_Implementation(USAFAsset* InAsset) {
	Asset = InAsset;
	ASAFPlayerState* MyOwner = ISAFActorInterface::Execute_GetOwningPlayer(this);
	ISAFActorInterface::Execute_InitFromAsset(this, InAsset, MyOwner, false);
}

void ASAFActor::InitFromAsset_Implementation(USAFAsset* InAsset, ASAFPlayerState* InOwner, bool bReinitialize) {
	// Initialization happens on the server
	if (!HasAuthority()) return;

	// Asset resolution
	Asset = InAsset ? InAsset : SAFAssetResolver::ResolveAsset(Asset);
	if (!Asset) { SAFDEBUG_ERROR("InitUnit failed: data is nullptr. Unit will be destroyed."); Destroy(); return; }
	else SAFDEBUG_INFO(FORMATSTR("Initializing new '%s' unit as '%s'.", *Asset->DisplayName.ToString(), *GetName()));

	// Initialization procedure
	ASAFPlayerState* ResolvedOwner = nullptr;
	if (!InOwner) {
		if (!SAFOwnershipResolver::ResolveOwner(GetWorld(), InitTeamID, InitPlayerID, ResolvedOwner)) 
			SAFDEBUG_ERROR("InitFromAsset: could not resolve owner.");
	} else ResolvedOwner = InOwner;
	
	ISAFActorInterface::Execute_SetOwningPlayer(this, ResolvedOwner);
	ForceNetUpdate();
}

// Sets the owning player (APlayerState*)
void ASAFActor::SetOwningPlayer_Implementation(ASAFPlayerState* InOwner) {
	if (HasAuthority()) { OwningPlayer = InOwner; ForceNetUpdate(); }
	else return;
}

// Gets the asset display name.
FText ASAFActor::GetDisplayName_Implementation() const {
	USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedAsset) { SAFDEBUG_WARNING("GetDisplayName failed: Asset was nullptr. Did you call this interface function before initialization?"); return FText(); }
	return ResolvedAsset->DisplayName;
}

// Gets the asset icon texture.
UTexture2D* ASAFActor::GetIcon_Implementation() const {
	USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedAsset) { SAFDEBUG_WARNING("GetIcon failed: Asset was nullptr. Did you call this interface function before initialization?"); return nullptr; }
	return ResolvedAsset->Icon.Get();
}

// Gets the asset portrait texture.
UTexture2D* ASAFActor::GetPortrait_Implementation() const {
	USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedAsset) { SAFDEBUG_WARNING("GetPortrait failed: Asset was nullptr. Did you call this interface function before initialization?"); return nullptr; }
	return ResolvedAsset->Icon.Get();
}

// Call to mark this as Selected.
// (marquee draw has been finalized/LMB released).
bool ASAFActor::Select_Implementation(AActor*& OutSelectedActor) {
	if (bSelectable) {
		OutSelectedActor = this;
		bIsSelected = true;
		OnSelectionChanged.Broadcast(bIsSelected);
		SAFDEBUG_INFO(FORMATSTR("Unit '%s' Selected successfully!", *this->GetName()));
		return true;
	} else { // Add additional handling to else block if needed
		SAFDEBUG_INFO(FORMATSTR("Unit '%s' rejected Selection: unit is not Selectable.", *this->GetName()));
		return false;
	}
}

// Call to mark this as QueueSelected (marquee is being drawn). There is no logging done 
// since queue selection runs on tick.
bool ASAFActor::QueueSelect_Implementation(AActor*& OutQueueSelectedActor) {
	if (bSelectable) {
		OutQueueSelectedActor = this;
		bIsQueueSelected = true;
		OnQueueSelectionChanged.Broadcast(bIsSelected);
		return true;
	} else return false;
}

// Call to mark this as deselected.
void ASAFActor::Deselect_Implementation() {
	if (bIsSelected) {
		bIsSelected = false;
		OnSelectionChanged.Broadcast(bIsSelected);
		SAFDEBUG_INFO(FORMATSTR("Unit '%s' Deselected.", *GetName()));
	} else SAFDEBUG_WARNING(FORMATSTR("Unit '%s' called Deselect() but it was not Selected!.", *GetName()));
}

// Call to mark this as no-longer QueSelected (i.i marquee area no longer overlaps
// it during marquee draw). There is no logging done since queue selection runs on tick.
void ASAFActor::DequeueSelect_Implementation() {
	if (bIsQueueSelected) {
		bIsQueueSelected = false;
		OnQueueSelectionChanged.Broadcast(bIsSelected);
	}
}

// Called when the SAFPlayerController attempts to place a SAFUnit queued for placement
void ASAFActor::Place_Implementation(FVector Location, FRotator Rotation) {
	SAFDEBUG_INFO(FORMATSTR("'%s' placed.", *this->GetName()));
	OnPlaced.Broadcast(Location, Rotation);
}

// Replication
// ==================================================================================================
void ASAFActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASAFActor, OwningPlayer);
}

void ASAFActor::OnRep_OwningPlayer() {
	SAFDEBUG_INFO(TEXT("OnRep_OwningPlayer triggered."));
}

#if WITH_EDITOR
void ASAFActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) {
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(ASAFActor, Asset)) {
		UpdateEditorPreview();
	}
}

void ASAFActor::PostLoad() {
	Super::PostLoad();
	
	// Create preview when loading from disk (editor only)
	if (GetWorld() && !GetWorld()->IsGameWorld()) {
		UpdateEditorPreview();
	}
}

void ASAFActor::OnConstruction(const FTransform& Transform) {
	Super::OnConstruction(Transform);
	
	// Handle editor vs runtime
	if (GetWorld() && GetWorld()->IsGameWorld()) {
		// Runtime - clear any editor preview components
		ClearEditorPreview();
	} else {
		// Editor - update preview
		UpdateEditorPreview();
	}
}

void ASAFActor::UpdateEditorPreview() {
	// Only run in editor, not in PIE or game
	if (!GetWorld() || GetWorld()->IsGameWorld()) {
		return;
	}

	ClearEditorPreview();

	USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedAsset) {
		return;
	}

	// Base implementation does nothing - override in subclasses
}

void ASAFActor::ClearEditorPreview() {
	if (EditorPreviewRoot) {
		EditorPreviewRoot->DestroyComponent();
		EditorPreviewRoot = nullptr;
	}
	
	// Also clean up any child components marked as editor-only
	TArray<UActorComponent*> Components;
	GetComponents(Components);
	for (UActorComponent* Component : Components) {
		if (Component && Component->bIsEditorOnly) {
			Component->DestroyComponent();
		}
	}
}
#endif
