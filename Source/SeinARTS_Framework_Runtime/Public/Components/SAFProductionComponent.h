#pragma once

#include "CoreMinimal.h"
#include "Assets/SAFAsset.h"
#include "Components/ArrowComponent.h"
#include "Structs/SAFProductionQueueItem.h" 
#include "Structs/SAFProductionRecipe.h" 
#include "Net/UnrealNetwork.h"
#include "SAFProductionComponent.generated.h"

class ASAFPlayerState;

/**
 * USAFProductionComponent
 *
 * Editor-visible (Arrow) production component that owns:
 * - A replicated production Catalogue of FSAFProductionRecipes seeded from UnitData.
 * - A replicated production queue of items to build (also USAFUnitData soft refs).
 * - Server-authoritative timing for the head-of-line item.
 *
 * Clients/UI call RequestEnqueue(UnitData); server validates against the live unlocked list.
 */
UCLASS(ClassGroup=(SeinARTS), BlueprintType, Blueprintable, 
meta=(BlueprintSpawnableComponent, DisplayName="SeinARTS Production Component"))
class SEINARTS_FRAMEWORK_RUNTIME_API USAFProductionComponent : public UArrowComponent {
	GENERATED_BODY()

public:

	USAFProductionComponent();

	// Catalogue + Queue
	// =======================================================================================================
	/** Replicated Catalogue of production recipes this component can produce. */
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing=OnRep_ProductionCatalogue, Category="SeinARTS")
	TArray<FSAFProductionRecipe> ProductionCatalogue;

	/** Replicated production queue (0 = head). */
	UPROPERTY(VisibleInstanceOnly, ReplicatedUsing=OnRep_ProductionQueue, Category="SeinARTS")
	TArray<FSAFProductionQueueItem> ProductionQueue;

	/** Remaining time (seconds) on the head-of-line item (server authoritative). */
	UPROPERTY(Replicated, VisibleInstanceOnly, Category="SeinARTS")
	float CurrentRemainingTime = 0.f;

	// Spawning / Rally Point
	// =======================================================================================================
	/** Optionally override the base arrow transform with a new spawn transform.
	 * Useful for off-map ingress, hologram placement system, etc... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Spawn & Rally Point")
	FTransform SpawnTransformOverride;

	/** Optional rally point for spawned units; if unset, uses the arrow's end/direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SeinARTS|Spawn & Rally Point")
	FVector RallyPoint = FVector::ZeroVector;

	// Production API
	// =======================================================================================================
	/** Seeds ProductionCatalogue from UnitData. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void InitProductionCatalogue(const TArray<FSAFProductionRecipe>& Recipes);

	/** True if the given UnitData is currently unlocked (exists in the ProductionCatalogue) and the
	 * production recipe is enabled. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	bool CanProduceAsset(const TSoftObjectPtr<USAFAsset>& Asset) const;

	/** Returns the active spawn transform of this component. The default transform is this component's
	 * transform offset to position at the end of the arrow. If SpawnTransformOverride is set, it
	 * will return it instead. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	FTransform GetSpawnTransform() const;

	/** Sets the spawn point override of this component, to overide the default spawn transform.
	 * If this is set, all FSAFQueueItems will use SpawnTransformOverride as their SpawnTransform
	 * fallback, instead of the arrow's end transform. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void SetSpawnTransformOverride(FTransform InTransform);

	/** Returns the spawn transform for a queue item in the queue at the index, if it exists. Useful
	 * for getting where an item will spawn if its spawn is overridden at queue time. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	FSAFProductionQueueItem GetQueueItemByIndex(int32 Index) const;

	/** Returns the active rally point. */
	UFUNCTION(BlueprintPure, Category="SeinARTS|Production")
	FVector GetRallyPoint() const;

	/** Sets the active rally point. (projects to navmesh by default) */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void SetRallyPoint(FVector InRallyPoint, bool bProjectToNavMesh = true);

	/** Get the UnitData soft reference for a Catalogue index (nullptr soft ref if out of range). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	FSAFProductionRecipe GetCatalogueRecipeByIndex(int32 CatalogueIndex) const;

	/** Enable (unlock) a recipe by its Catalogue index. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void EnableRecipeByIndex(int32 CatalogueIndex);

	/** Disable (lock) a recipe by its Catalogue index; removes queued entries for that unit. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void DisableRecipeByIndex(int32 CatalogueIndex);

	/** Enable (unlock) a recipe by UnitData reference. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void EnableRecipeByData(const TSoftObjectPtr<USAFAsset>& Asset);

	/** Disable (lock) a recipe by UnitData reference; removes queued entries for that unit. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void DisableRecipeByData(const TSoftObjectPtr<USAFAsset>& Asset);

	/** Client/UI call. Server validates and enqueues if allowed.
	 * This allows clients to set custom per-item spanw transforms (for placement systems, etc.).
	 * If spawn transform isn't set, it will fallback to the classes transform or its override.
	 * Use third param to tell the unit to route to rally point (if set & able) on spawn. If rally
	 * point is not set at time of spawn, nothing will happen. */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production", meta=(AutoCreateRefTerm="SpawnTransform", AdvancedDisplay="SpawnTransform"))
	void RequestEnqueue(TSoftObjectPtr<USAFAsset> Asset, const FTransform& SpawnTransform, bool bRouteToRallyPoint = true);

	/** Client/UI call to cancel the queue entry at Index (0=head). */
	UFUNCTION(BlueprintCallable, Category="SeinARTS|Production")
	void RequestCancellation(int32 Index);

protected:

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

	// Internals
	// =========================================================================================================
	// Internal (server): progress the head-of-line item (wire to timer/tick as desired).
	void AdvanceBuild(float DeltaSeconds);

	// Internal (server): finalize a completed build (spawn, rally, notify).
	void CompleteBuild(FSAFProductionQueueItem CompletedItem);

	// Internal (server): Append Ref to List only if it's not already present.
	void AddItemToCatalogue(const TSoftObjectPtr<USAFAsset>& Asset, bool bEnabled = true);

	// Internal (server): Remove all occurrences of UnitData from the production catalogue.
	void RemoveItemFromCatalogue(const TSoftObjectPtr<USAFAsset>& Asset);

	// Safely resolves the BuildTime of a UnitData from its AttributesRow (defaults to 1.f if absent/invalid).
	float ResolveBuildTime(USAFAsset* Asset) const;

	// Safely resolves owner's BuildSpeed from SAFAttributes (defaults to 1.f if absent/invalid).
	float ResolveBuildSpeed() const;

	// Safely resolves the costs for a unit via unit data (runtime preferred, fallback to defaults).
	FSAFResources ResolveCostsByData(USAFAsset* Asset) const;

	// Gets the SAFPlayerState of the owning actor's owning controller, if any.
	ASAFPlayerState* GetSAFPlayerState() const;

	// Replication
	// ======================================================================================================================================================
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION() void OnRep_ProductionCatalogue();
	UFUNCTION() void OnRep_ProductionQueue();
	UFUNCTION(Server, Reliable) void Server_Enqueue(const TSoftObjectPtr<USAFAsset>& Asset, const FTransform& SpawnTransform, bool bRouteToRallyPoint = true);
	UFUNCTION(Server, Reliable) void Server_CancelAtIndex(int32 Index);
	UFUNCTION(Server, Reliable) void Server_EnableRecipeByIndex(int32 CatalogueIndex);
	UFUNCTION(Server, Reliable) void Server_DisableRecipeByIndex(int32 CatalogueIndex);
	UFUNCTION(Server, Reliable) void Server_EnableRecipeByData(const TSoftObjectPtr<USAFAsset>& Asset);
	UFUNCTION(Server, Reliable) void Server_DisableRecipeByData(const TSoftObjectPtr<USAFAsset>& Asset);

};
