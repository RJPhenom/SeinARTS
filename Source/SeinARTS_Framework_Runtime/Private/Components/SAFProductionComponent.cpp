#include "Components/SAFProductionComponent.h"
#include "Classes/Unreal/SAFPlayerState.h"
#include "Structs/SAFOrder.h"
#include "Assets/SAFAsset.h"
#include "Gameplay/Attributes/SAFUnitAttributes.h"
#include "Gameplay/Attributes/SAFProductionAttributes.h"
#include "Engine/World.h"
#include "Interfaces/SAFActorInterface.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"  
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"
#include "Resolvers/SAFAssetResolver.h"
#include "Structs/SAFAttributesRow.h"
#include "Structs/SAFResources.h"
#include "Utils/SAFLibrary.h"
#include "Debug/SAFDebugTool.h"
#include "DrawDebugHelpers.h"

USAFProductionComponent::USAFProductionComponent() {
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void USAFProductionComponent::BeginPlay() {
	Super::BeginPlay();
}

void USAFProductionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) {  
	AdvanceBuild(DeltaTime);
	SAFDEBUG_DRAWARROWSPHERE(0.f, FColor::Red);
}

// Override is used to clear queue and issue refunds before object destruction, and to cull any
// orphaned builds if the component is removed at runtime.
void USAFProductionComponent::OnUnregister() {
	if (GetOwnerRole() == ROLE_Authority) {
		// TODO: Refund logic per queued item (resources, popcap). Hook your resource system here.
		ProductionQueue.Reset();
		CurrentRemainingTime = 0.f;
	}
	
	Super::OnUnregister();
}

// Seeds ProductionCatalogue from Asset.
void USAFProductionComponent::InitProductionCatalogue(const TArray<FSAFProductionRecipe>& Recipes) {
	if (GetOwnerRole() != ROLE_Authority) return;
	ProductionCatalogue = Recipes;
}

// True if the given Asset is currently unlocked (exists in the ProductionCatalogue) and the 
// production recipe is enabled.
bool USAFProductionComponent::CanProduceAsset(const TSoftObjectPtr<USAFAsset>& Asset) const {
	for (const FSAFProductionRecipe& Recipe : ProductionCatalogue) if (Recipe.bEnabled && SAFLibrary::SoftEqual(Recipe.Asset, Asset)) return true;
	return false;
}

// Returns the active spawn point. (projects to navmesh by default)
FTransform USAFProductionComponent::GetSpawnTransform() const {
	if (!SpawnTransformOverride.Equals(FTransform::Identity)) return SpawnTransformOverride;
	FTransform SpawnTransform = GetComponentTransform();
	const FVector ArrowTip = SpawnTransform.TransformPosition(FVector(ArrowLength * ArrowSize, 0.f, 0.f));
	SpawnTransform.SetLocation(ArrowTip);
	return SpawnTransform;
}

// Sets the spawn point override, to overide the default spawn point.
void USAFProductionComponent::SetSpawnTransformOverride(FTransform InTransform) {
	SpawnTransformOverride = InTransform;
}

// Returns the spawn transform for a queue item in the queue at the index, if it exists. Useful 
// for getting where an item will spawn if its spawn is overridden at queue time.
FSAFProductionQueueItem USAFProductionComponent::GetQueueItemByIndex(int32 Index) const {
	return ProductionQueue.IsValidIndex(Index) ? ProductionQueue[Index] : FSAFProductionQueueItem();
}

// Returns the active rally point.
FVector USAFProductionComponent::GetRallyPoint() const {
	if (!RallyPoint.IsNearlyZero()) return RallyPoint;
	const FVector ArrowTip = GetComponentLocation() + GetForwardVector() * ArrowLength;
	return ArrowTip;
}

// Sets the active rally point. (projects to navmesh by default)
void USAFProductionComponent::SetRallyPoint(FVector InRallyPoint, bool bProjectToNavMesh) {
	if (bProjectToNavMesh) {
		if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld())) {
			FNavLocation ProjectedRallyPoint;
			const FVector QueryExtent(150.f, 150.f, 500.f);
			if (NavSys->ProjectPointToNavigation(InRallyPoint, ProjectedRallyPoint, QueryExtent)) {
				RallyPoint = ProjectedRallyPoint.Location;
				return;
			} else SAFDEBUG_WARNING("SetRallyPoint: failed to project point to navmesh, falling back to raw vector.");
		} else SAFDEBUG_WARNING("SetRallyPoint: failed to find navigation system, falling back to raw vector.");
	}

	RallyPoint = InRallyPoint;
}

// Get the Asset soft ref for a Catalogue index (nullptr soft ref if out of range).
FSAFProductionRecipe USAFProductionComponent::GetCatalogueRecipeByIndex(int32 CatalogueIndex) const	{
	return ProductionCatalogue.IsValidIndex(CatalogueIndex) ? ProductionCatalogue[CatalogueIndex] : FSAFProductionRecipe();
}

// Enable (unlock) a recipe by its Catalogue index.
void USAFProductionComponent::EnableRecipeByIndex(int32 CatalogueIndex) {
	Server_EnableRecipeByIndex(CatalogueIndex);
}

// Disable (lock) a recipe by its Catalogue index; removes queued entries for that unit.
void USAFProductionComponent::DisableRecipeByIndex(int32 CatalogueIndex) {
	Server_DisableRecipeByIndex(CatalogueIndex);
}

// Enable (unlock) a recipe by Asset reference.
void USAFProductionComponent::EnableRecipeByData(const TSoftObjectPtr<USAFAsset>& Asset) {
	if (Asset.IsNull()) return;
	Server_EnableRecipeByData(Asset);
}

// Disable (lock) a recipe by Asset reference; removes queued entries for that unit.
void USAFProductionComponent::DisableRecipeByData(const TSoftObjectPtr<USAFAsset>& Asset) {
	if (Asset.IsNull()) return;
	Server_DisableRecipeByData(Asset);
}

// Client/UI call. Server validates and enqueues if allowed.
void USAFProductionComponent::RequestEnqueue(TSoftObjectPtr<USAFAsset> Asset, const FTransform& SpawnTransform, bool bRouteToRallyPoint) {
	if (Asset.IsNull()) { SAFDEBUG_WARNING("RequestEnqueue called on null Asset. Discarding."); return; }
	if (!CanProduceAsset(Asset)) { SAFDEBUG_WARNING("RequestEnqueue called on Asset that is not in catalogue. Discarding."); return; }

	USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedAsset) { SAFDEBUG_WARNING("RequestEnqueue called, but Asset could not be resolved. Aborting."); return; }

	// Check if there are enough resources/resources are present, does not mutates player state resources.
	FSAFResources Costs = ResolveCostsByData(ResolvedAsset);
	ASAFPlayerState* SAFPlayerState = GetSAFPlayerState();
	if (!SAFPlayerState) { SAFDEBUG_WARNING("Server_Enqueue called, but the Owner's player state is the wrong type. Aborting."); return; }
	if (!SAFPlayerState->CheckResourcesAvailable(Costs)) { 
		SAFDEBUG_INFO(FORMATSTR("RequestEnqueue: Actor '%s' requesting enqueue: '%s' failed: not enough resources.", *GetName(), *ResolvedAsset->DisplayName.ToString())); 
		return; 
	}
	
	SAFDEBUG_INFO(FORMATSTR("RequestEnqueue: Actor '%s' requesting enqueue: '%s' succeed. Sending request to server.", *GetName(), *ResolvedAsset->DisplayName.ToString()));
	Server_Enqueue(Asset, SpawnTransform, bRouteToRallyPoint);
}

// Client/UI call to cancel the queue entry at Index (0=head).
void USAFProductionComponent::RequestCancellation(int32 Index) {
	Server_CancelAtIndex(Index);
}

// Internals
// ===================================================================================================================================
// Internal (server): progress the head-of-line item (wire to timer/tick as desired).
void USAFProductionComponent::AdvanceBuild(float DeltaSeconds) {
	if (GetOwnerRole() != ROLE_Authority || ProductionQueue.Num() == 0) return;

	const float BuildSpeed = ResolveBuildSpeed();
	if (CurrentRemainingTime > 0.f) CurrentRemainingTime = FMath::Max(0.f, CurrentRemainingTime - DeltaSeconds * BuildSpeed);
	if (CurrentRemainingTime <= 0.f) {
		FSAFProductionQueueItem CompletedItem = ProductionQueue[0];
		ProductionQueue.RemoveAt(0);
		CompleteBuild(CompletedItem);

		// Move to next item
		if (ProductionQueue.Num() > 0)
			if (USAFAsset* NextAsset = SAFAssetResolver::ResolveAsset(ProductionQueue[0].Asset))
				CurrentRemainingTime = FMath::Max(0.f, ResolveBuildTime(NextAsset));
	}
}

// Internal (server): finalize a completed build (spawn, rally, notify).
void USAFProductionComponent::CompleteBuild(FSAFProductionQueueItem CompletedItem) {
	USAFAsset* ResolvedData = SAFAssetResolver::ResolveAsset(CompletedItem.Asset);
	UWorld* World = GetWorld();
	if (!ResolvedData) { SAFDEBUG_WARNING("CompleteBuild called on null Asset. Discarding."); return; }
	if (!World) { SAFDEBUG_WARNING("CompleteBuild called on null world. Discarding."); return; }

	// Check valid spawn class
	UClass* InstanceClass = ResolvedData->InstanceClass.LoadSynchronous();
	if (!InstanceClass) { SAFDEBUG_WARNING("CompletedBuild called, but Asset has invalid class. Spawning aborted."); return; }

	// Compute spawn transform (override if provided, else use arrow transform)
	const FTransform SpawnTransform = !CompletedItem.SpawnTransform.Equals(FTransform::Identity)
		? CompletedItem.SpawnTransform
		: GetSpawnTransform();

	// Finish params and aspawn
	FActorSpawnParameters Params;
	Params.Owner = GetOwner();
	Params.Instigator = Cast<APawn>(GetOwner());
	AActor* NewActor= World->SpawnActorDeferred<AActor>(
		InstanceClass,
		SpawnTransform,
		GetOwner(),
		Params.Instigator,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn
	);

	// If the spawned class doesn't implement the unit interface.
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(NewActor)) {
		SAFDEBUG_WARNING("CompleteBuild built an invalid actor. Culling (if extant).");
		if (NewActor) NewActor->Destroy();
		return;
	}

	NewActor->FinishSpawning(SpawnTransform);
	SAFDEBUG_SUCCESS(FORMATSTR("CompleteBuild successful. New unit '%s' was built, %d units remaining in queue.", *NewActor->GetName(), ProductionQueue.Num()));

	AActor* Owner = GetOwner();
	if (SAFLibrary::IsActorPtrValidSeinARTSUnit(Owner)) { 
		ASAFPlayerState* MyOwner = ISAFActorInterface::Execute_GetOwningPlayer(Owner);
		ISAFActorInterface::Execute_InitFromAsset(NewActor, ResolvedData, MyOwner, true /** Force reinit */);
	} else SAFDEBUG_WARNING("CompleteBuild: Component owner is not a valid unit. NewActor will have no owner set.");

	if (CompletedItem.bRouteToRallyPoint) {
		FSAFOrder Order(nullptr, nullptr, GetComponentLocation(), GetRallyPoint(), FGameplayTag::RequestGameplayTag(TEXT("SeinARTS.Order.Move")));
		// TODO: issue move order to rally point
	}
}

// Append Asset to the production catalog (does nothing if it's already present).
void USAFProductionComponent::AddItemToCatalogue(const TSoftObjectPtr<USAFAsset>& Asset, bool bEnabled) {
	for (const FSAFProductionRecipe& Recipe : ProductionCatalogue) 
		if (SAFLibrary::SoftEqual(Recipe.Asset, Asset)) return;
	ProductionCatalogue.Emplace(Asset, bEnabled);
}

// Remove all occurrences of Asset from the production catalogue.
void USAFProductionComponent::RemoveItemFromCatalogue(const TSoftObjectPtr<USAFAsset>& Asset) {
	for (int32 i = ProductionCatalogue.Num() - 1; i >= 0; --i)	
		if (SAFLibrary::SoftEqual(ProductionCatalogue[i].Asset, Asset)) 
			ProductionCatalogue.RemoveAt(i);
}

// Safely resolves the BuildTime of a Asset from its AttributesRow (defaults to 1.f if absent/invalid).
float USAFProductionComponent::ResolveBuildTime(USAFAsset* Asset) const {
	if (!Asset) return 1.f;
	// if (const FSAFAttributesRow* Row = Asset->AttributeRow.GetRow<FSAFAttributesRow>(TEXT("ResolveBuildTime"))) {
	// 	const float Val = Row->BuildTime;
	// 	if (FMath::IsFinite(Val) && Val > 0.f) return Val;
	// }

	return 1.f;
}

// Safely resolves owner's BuildSpeed from SAFAttributes (defaults to 1.f if absent/invalid).
float USAFProductionComponent::ResolveBuildSpeed() const {
	const AActor* Owner = GetOwner(); if (!Owner) return 1.f;
	const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Owner); if (!ASI) return 1.f;
	const UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent(); if (!ASC) return 1.f;
	const USAFUnitAttributes* Attributes = ASC->GetSet<USAFUnitAttributes>(); if (!Attributes) return 1.f;
	const float Value = 1.f; //TODO: Attributes ? Attributes->GetBuildSpeed() : 1.f;
	return (Value > 0.f) ? Value : 0.f;
}

// Safely resolves the costs for a unit via unit data (runtime preferred, fallback to defaults).
FSAFResources USAFProductionComponent::ResolveCostsByData(USAFAsset* Asset) const {
	AActor* Actor = GetOwner();
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) return FSAFResources{}; //Asset->GetDefaultCosts();
	const APlayerState* PlayerState = ISAFActorInterface::Execute_GetOwningPlayer(GetOwner());
	return FSAFResources{}; //Asset->GetRuntimeCosts(PlayerState);
}

// Gets the SAFPlayerState of the owning actor's owning controller, if any.
ASAFPlayerState* USAFProductionComponent::GetSAFPlayerState() const {
	AActor* Owner = GetOwner();

	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Owner)) return nullptr;
	APlayerState* PlayerState = ISAFActorInterface::Execute_GetOwningPlayer(Owner);

	if (!PlayerState) return nullptr;
	return Cast<ASAFPlayerState>(PlayerState);
}

// Replication
// ===========================================================================================================================================================
void USAFProductionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USAFProductionComponent, ProductionCatalogue);
	DOREPLIFETIME(USAFProductionComponent, ProductionQueue);
	DOREPLIFETIME(USAFProductionComponent, CurrentRemainingTime);
}

void USAFProductionComponent::OnRep_ProductionCatalogue() {
	// Client-side UI can refresh availability states here.
}

void USAFProductionComponent::OnRep_ProductionQueue() {
	// Client-side UI/audio hook for queue changes.
}

void USAFProductionComponent::Server_Enqueue_Implementation(const TSoftObjectPtr<USAFAsset>& Asset, const FTransform& SpawnTransform, bool bRouteToRallyPoint) {
	USAFAsset* ResolvedData = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedData) { SAFDEBUG_WARNING("Server_Enqueue called, but Asset could not be resolved. Aborting."); return; }

	// Confirm allowed (is in the catalogue)
	bool bAllowed = false;
	for (const FSAFProductionRecipe& Recipe : ProductionCatalogue)
		if (Recipe.bEnabled && SAFLibrary::SoftEqual(Recipe.Asset, Asset)) { bAllowed = true;	break; }
	if (!bAllowed) { SAFDEBUG_INFO("Server_Enqueue called on absent or disabled Asset. Discarding."); return; }

	// Request resources, mutate player state resources (handle deductions/refunds).	
	FSAFResources Costs = ResolveCostsByData(ResolvedData);
	ASAFPlayerState* SAFPlayerState = GetSAFPlayerState();
	if (!SAFPlayerState) { SAFDEBUG_WARNING("Server_Enqueue called, but the Owner's player state is the wrong type. Aborting."); return; }
	if (!SAFPlayerState->RequestResources(Costs)) { 
		SAFDEBUG_INFO(FORMATSTR("Server_Enqueue: Actor '%s' requesting enqueue: '%s' failed on: not enough resources.", *GetName(), *Asset.Get()->DisplayName.ToString())); 
		return; 
	}

	// If we got here, the unit is in the catalogue and player has the resources needed.
	// Proceeds to build the queue item and insert it.
	FSAFProductionQueueItem QueueItem = FSAFProductionQueueItem(
		Asset,
		SpawnTransform.Equals(FTransform::Identity) ? SpawnTransform : GetSpawnTransform(),
		bRouteToRallyPoint,
		Costs
	);

	SAFDEBUG_SUCCESS(FORMATSTR("Server_Enqueue: Validation successful. Accepting enqueue request for: '%s'", *Asset.Get()->DisplayName.ToString()));
	ProductionQueue.Add(QueueItem);
	if (ProductionQueue.Num() == 1) CurrentRemainingTime = FMath::Max(0.f, ResolveBuildTime(ResolvedData));
}

void USAFProductionComponent::Server_CancelAtIndex_Implementation(int32 Index) {
	if (!ProductionQueue.IsValidIndex(Index)) return;

	// Grab item and refund (if any) then remove the item from the queue
	FSAFProductionQueueItem QueueItem = ProductionQueue[Index];
	FSAFResources Refund = QueueItem.CostsBundle;
	const bool bHead = (Index == 0);
	ProductionQueue.RemoveAt(Index);

	// Refunds the costs bundle stored in the queue item
	ASAFPlayerState* SAFPlayerState = GetSAFPlayerState();
	if (!SAFPlayerState) { SAFDEBUG_WARNING("Server_Enqueue called, but the Owner's player state is the wrong type. Aborting."); return; }
	SAFPlayerState->AddResources(Refund);

	if (!bHead) return;
	if (!(ProductionQueue.Num() > 0)) return;
	if (USAFAsset* NextData = SAFAssetResolver::ResolveAsset(ProductionQueue[0].Asset)) CurrentRemainingTime = FMath::Max(0.f, ResolveBuildTime(NextData));
	else CurrentRemainingTime = 0.f;
}

void USAFProductionComponent::Server_EnableRecipeByIndex_Implementation(int32 CatalogIndex) {
	if (!ProductionCatalogue.IsValidIndex(CatalogIndex)) return;
	ProductionCatalogue[CatalogIndex].bEnabled = true;
}

void USAFProductionComponent::Server_DisableRecipeByIndex_Implementation(int32 CatalogIndex) {
	if (!ProductionCatalogue.IsValidIndex(CatalogIndex)) return;

	const TSoftObjectPtr<USAFAsset> Target = ProductionCatalogue[CatalogIndex].Asset;
	ProductionCatalogue[CatalogIndex].bEnabled = false;

	// Drop queued entries for this unit
	for (int32 i = ProductionQueue.Num() - 1; i >= 0; --i) {
		if (SAFLibrary::SoftEqual(ProductionQueue[i].Asset, Target)) {
			const bool bHead = (i == 0);
			ProductionQueue.RemoveAt(i);
			if (bHead) {
				if (ProductionQueue.Num() > 0) {
					if (USAFAsset* NextData = SAFAssetResolver::ResolveAsset(ProductionQueue[0].Asset)) 
						CurrentRemainingTime = FMath::Max(0.f, ResolveBuildTime(NextData));
				} else CurrentRemainingTime = 0.f;
			}
		}
	}
}

void USAFProductionComponent::Server_EnableRecipeByData_Implementation(const TSoftObjectPtr<USAFAsset>& Asset) {
	if (Asset.IsNull()) return;

	for (FSAFProductionRecipe& Recipe : ProductionCatalogue) {
		if (SAFLibrary::SoftEqual(Recipe.Asset, Asset)) {
			Recipe.bEnabled = true;
			return;
		}
	}
}

void USAFProductionComponent::Server_DisableRecipeByData_Implementation(const TSoftObjectPtr<USAFAsset>& Asset) {
	if (Asset.IsNull()) return;

	for (FSAFProductionRecipe& Recipe : ProductionCatalogue) {
		if (SAFLibrary::SoftEqual(Recipe.Asset, Asset)) {
			Recipe.bEnabled = false;
			break;
		}
	}

	// Drop queued entries for this unit
	for (int32 i = ProductionQueue.Num() - 1; i >= 0; --i) {
		if (SAFLibrary::SoftEqual(ProductionQueue[i].Asset, Asset)) {
			const bool bHead = (i == 0);
			ProductionQueue.RemoveAt(i);
			if (bHead) {
				if (ProductionQueue.Num() > 0) {
					if (USAFAsset* NextData = SAFAssetResolver::ResolveAsset(ProductionQueue[0].Asset))
						CurrentRemainingTime = FMath::Max(0.f, ResolveBuildTime(NextData));
				} else CurrentRemainingTime = 0.f;
			}
		}
	}
}