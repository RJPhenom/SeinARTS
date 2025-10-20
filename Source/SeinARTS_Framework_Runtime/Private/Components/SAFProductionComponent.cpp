#include "Components/SAFProductionComponent.h"
#include "Components/SAFPlayerTechnologyComponent.h"
#include "Assets/SAFTechnologyAsset.h"
#include "Assets/SAFUnitAsset.h"
#include "Classes/SAFActor.h"
#include "Classes/SAFPlayerState.h"
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
#include "Structs/SAFResourceBundle.h"
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

	const USAFAsset* ResolvedAsset = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedAsset) { SAFDEBUG_WARNING("RequestEnqueue called, but Asset could not be resolved. Aborting."); return; }

	// Check if there are enough resources using technology-aware cost calculation
	FSAFResourceBundle EffectiveCosts = ResolveEffectiveResourceCosts(ResolvedAsset);
	ASAFPlayerState* SAFPlayerState = GetSAFPlayerState();
	if (!SAFPlayerState) { SAFDEBUG_WARNING("RequestEnqueue called, but the Owner's player state is the wrong type. Aborting."); return; }
	if (!SAFPlayerState->CheckResourcesAvailable(EffectiveCosts)) { 
		SAFDEBUG_INFO(FORMATSTR("RequestEnqueue: Actor '%s' requesting enqueue: '%s' failed: not enough resources (need %s).", 
			*GetName(), *ResolvedAsset->DisplayName.ToString(), *EffectiveCosts.ToString())); 
		return; 
	}
	
	SAFDEBUG_INFO(FORMATSTR("RequestEnqueue: Actor '%s' requesting enqueue: '%s' succeed. Sending request to server.", 
		*GetName(), *ResolvedAsset->DisplayName.ToString()));
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
		if (ProductionQueue.Num() > 0) {
			if (USAFAsset* NextAsset = SAFAssetResolver::ResolveAsset(ProductionQueue[0].Asset)) {
				// Use technology-aware build time resolution
				CurrentRemainingTime = FMath::Max(0.f, ResolveEffectiveBuildTime(NextAsset));
			}
		}
	}
}

// Internal (server): finalize a completed build (spawn, rally, notify).
void USAFProductionComponent::CompleteBuild(FSAFProductionQueueItem CompletedItem) {
	USAFAsset* ResolvedData = SAFAssetResolver::ResolveAsset(CompletedItem.Asset);
	UWorld* World = GetWorld();
	if (!ResolvedData) { SAFDEBUG_WARNING("CompleteBuild called on null Asset. Discarding."); return; }
	if (!World) { SAFDEBUG_WARNING("CompleteBuild called on null world. Discarding."); return; }

	// Check if this is a technology research completion
	if (Cast<USAFTechnologyAsset>(ResolvedData)) {
		CompleteTechnologyResearch(CompletedItem);
		return;
	}

	// Choose base spawn class / override if set
	UClass* InstanceClass = ResolvedData->InstanceClass ? 
		ResolvedData->InstanceClass.LoadSynchronous() : ASAFActor::StaticClass();

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
		FSAFVectorSet InVectors;
		InVectors.Start = GetComponentLocation();
		InVectors.End = GetRallyPoint();

		FSAFOrder Order(nullptr, nullptr, InVectors, FGameplayTag::RequestGameplayTag(TEXT("SeinARTS.Order.Move")));
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
FSAFResourceBundle USAFProductionComponent::ResolveCostsByData(USAFAsset* Asset) const {
	AActor* Actor = GetOwner();
	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Actor)) return FSAFResourceBundle{}; //Asset->GetDefaultCosts();
	const APlayerState* PlayerState = ISAFActorInterface::Execute_GetOwningPlayer(GetOwner());
	return FSAFResourceBundle{}; //Asset->GetRuntimeCosts(PlayerState);
}

// Gets the SAFPlayerState of the owning actor's owning controller, if any.
ASAFPlayerState* USAFProductionComponent::GetSAFPlayerState() const {
	AActor* Owner = GetOwner();

	if (!SAFLibrary::IsActorPtrValidSeinARTSUnit(Owner)) return nullptr;
	APlayerState* PlayerState = ISAFActorInterface::Execute_GetOwningPlayer(Owner);

	if (!PlayerState) return nullptr;
	return Cast<ASAFPlayerState>(PlayerState);
}

// Technology Integration
// =======================================================================================================
FSAFResourceBundle USAFProductionComponent::ResolveEffectiveResourceCosts(const USAFAsset* Asset) const {
	if (!Asset) return FSAFResourceBundle{};
	
	// Get base costs from asset
	FSAFResourceBundle BaseCosts = ResolveCostsByData(const_cast<USAFAsset*>(Asset));
	
	// Apply technology modifications through player technology component
	if (USAFPlayerTechnologyComponent* PlayerTechComp = GetPlayerTechnologyComponent()) {
		// For technology assets, let the technology component handle it directly
		if (Cast<USAFTechnologyAsset>(Asset)) return PlayerTechComp->ResolveEffectiveResourceCosts(Asset);
		
		// For unit assets, combine base costs with technology modifiers
		FGameplayTagContainer AssetTypeTags;
		if (const USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(Asset)) AssetTypeTags = UnitAsset->Tags;
		
		// Calculate technology-based cost multiplier
		float CostMultiplier = PlayerTechComp->CalculateCostMultiplier(AssetTypeTags);
		
		// Apply multiplier to base costs
		FSAFResourceBundle ModifiedCosts = BaseCosts;
		ModifiedCosts.Resource1 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource1 * CostMultiplier);
		ModifiedCosts.Resource2 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource2 * CostMultiplier);
		ModifiedCosts.Resource3 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource3 * CostMultiplier);
		ModifiedCosts.Resource4 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource4 * CostMultiplier);
		ModifiedCosts.Resource5 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource5 * CostMultiplier);
		ModifiedCosts.Resource6 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource6 * CostMultiplier);
		ModifiedCosts.Resource7 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource7 * CostMultiplier);
		ModifiedCosts.Resource8 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource8 * CostMultiplier);
		ModifiedCosts.Resource9 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource9 * CostMultiplier);
		ModifiedCosts.Resource10 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource10 * CostMultiplier);
		ModifiedCosts.Resource11 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource11 * CostMultiplier);
		ModifiedCosts.Resource12 = FSAFResourceBundle::ToInt(ModifiedCosts.Resource12 * CostMultiplier);
		
		return ModifiedCosts;
	}
	
	return BaseCosts;
}

float USAFProductionComponent::ResolveEffectiveBuildTime(const USAFAsset* Asset) const {
	if (!Asset) return 0.f;
	
	// Get base build time
	float BaseBuildTime = ResolveBuildTime(const_cast<USAFAsset*>(Asset));
	
	// Apply technology modifications through player technology component
	if (USAFPlayerTechnologyComponent* PlayerTechComp = GetPlayerTechnologyComponent()) {
		// For technology assets, let the technology component handle it directly
		if (Cast<USAFTechnologyAsset>(Asset)) return PlayerTechComp->ResolveEffectiveBuildTime(Asset);
		
		// For unit assets, combine base time with technology modifiers
		FGameplayTagContainer AssetTypeTags;
		if (const USAFUnitAsset* UnitAsset = Cast<USAFUnitAsset>(Asset)) AssetTypeTags = UnitAsset->Tags;
		
		// Calculate technology-based time multiplier
		float TimeMultiplier = PlayerTechComp->CalculateTimeMultiplier(AssetTypeTags);
		
		// Apply multiplier to base time
		float ModifiedBuildTime = BaseBuildTime * TimeMultiplier;
		
		// Ensure minimum build time
		return FMath::Max(ModifiedBuildTime, 0.1f);
	}
	
	return BaseBuildTime;
}

USAFPlayerTechnologyComponent* USAFProductionComponent::GetPlayerTechnologyComponent() const {
	ASAFPlayerState* PlayerState = GetSAFPlayerState();
	if (!PlayerState) return nullptr;
	return PlayerState->GetComponentByClass<USAFPlayerTechnologyComponent>();
}

void USAFProductionComponent::CompleteTechnologyResearch(FSAFProductionQueueItem CompletedItem) {
	USAFTechnologyAsset* TechAsset = Cast<USAFTechnologyAsset>(SAFAssetResolver::ResolveAsset(CompletedItem.Asset));
	if (!TechAsset) { SAFDEBUG_WARNING("CompleteTechnologyResearch called but asset is not a technology asset."); return; }
	
	// Research the technology through the player technology component
	if (USAFPlayerTechnologyComponent* PlayerTechComp = GetPlayerTechnologyComponent()) {
		if (PlayerTechComp->ResearchTechnology(TechAsset)) 
			SAFDEBUG_SUCCESS(FORMATSTR("Successfully completed research of technology '%s'.", *TechAsset->GetName()));
		else SAFDEBUG_WARNING(FORMATSTR("Failed to complete research of technology '%s'.", *TechAsset->GetName()));
	} else SAFDEBUG_WARNING("CompleteTechnologyResearch: No player technology component found.");
}

void USAFProductionComponent::AddOrEnableProductionRecipes(const TArray<FSAFProductionRecipe>& NewRecipes) {
	if (GetOwnerRole() != ROLE_Authority) return;
	
	for (FSAFProductionRecipe NewRecipe : NewRecipes) {
		if (NewRecipe.Asset.IsNull()) continue;
		
		// Check if recipe already exists, enable it
		bool bFound = false;
		for (FSAFProductionRecipe& ExistingRecipe : ProductionCatalogue) {
			if (SAFLibrary::SoftEqual(ExistingRecipe.Asset, NewRecipe.Asset)) {
				ExistingRecipe.bEnabled = true;
				bFound = true;
				break;
			}
		}
		
		// Add new recipe if not found
		if (!bFound) {
			NewRecipe.bEnabled = true;
			ProductionCatalogue.Add(NewRecipe);
		}
	}
	
	SAFDEBUG_SUCCESS(FORMATSTR("Added/enabled %d production recipes from technology unlock.", NewRecipes.Num()));
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
	const USAFAsset* ResolvedData = SAFAssetResolver::ResolveAsset(Asset);
	if (!ResolvedData) { SAFDEBUG_WARNING("Server_Enqueue called, but Asset could not be resolved. Aborting."); return; }

	// Confirm allowed (is in the catalogue)
	bool bAllowed = false;
	for (const FSAFProductionRecipe& Recipe : ProductionCatalogue)
		if (Recipe.bEnabled && SAFLibrary::SoftEqual(Recipe.Asset, Asset)) { bAllowed = true;	break; }
	if (!bAllowed) { SAFDEBUG_INFO("Server_Enqueue called on absent or disabled Asset. Discarding."); return; }

	// Request resources using technology-aware cost calculation
	FSAFResourceBundle EffectiveCosts = ResolveEffectiveResourceCosts(ResolvedData);
	ASAFPlayerState* SAFPlayerState = GetSAFPlayerState();
	if (!SAFPlayerState) { SAFDEBUG_WARNING("Server_Enqueue called, but the Owner's player state is the wrong type. Aborting."); return; }
	if (!SAFPlayerState->RequestResources(EffectiveCosts)) { 
		SAFDEBUG_INFO(FORMATSTR("Server_Enqueue: Actor '%s' requesting enqueue: '%s' failed on: not enough resources (need %s).", 
			*GetName(), *ResolvedData->DisplayName.ToString(), *EffectiveCosts.ToString())); 
		return; 
	}

	// If we got here, the unit is in the catalogue and player has the resources needed.
	// Proceeds to build the queue item and insert it.
	FSAFProductionQueueItem QueueItem = FSAFProductionQueueItem(
		Asset,
		SpawnTransform.Equals(FTransform::Identity) ? SpawnTransform : GetSpawnTransform(),
		bRouteToRallyPoint,
		EffectiveCosts
	);

	SAFDEBUG_SUCCESS(FORMATSTR("Server_Enqueue: Validation successful. Accepting enqueue request for: '%s' (effective costs: %s)", 
		*ResolvedData->DisplayName.ToString(), *EffectiveCosts.ToString()));
	ProductionQueue.Add(QueueItem);
	
	// Use technology-aware build time for the first item in queue
	if (ProductionQueue.Num() == 1) {
		CurrentRemainingTime = FMath::Max(0.f, ResolveEffectiveBuildTime(ResolvedData));
	}
}

void USAFProductionComponent::Server_CancelAtIndex_Implementation(int32 Index) {
	if (!ProductionQueue.IsValidIndex(Index)) return;

	// Grab item and refund (if any) then remove the item from the queue
	FSAFProductionQueueItem QueueItem = ProductionQueue[Index];
	FSAFResourceBundle Refund = QueueItem.CostsBundle;
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