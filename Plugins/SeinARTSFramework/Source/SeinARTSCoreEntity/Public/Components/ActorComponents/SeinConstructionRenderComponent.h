/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:    SeinConstructionRenderComponent.h
 * @brief:   Render-side visual response to construction state changes. Pure
 *           UE-native UActorComponent (NOT a USeinActorComponent subclass) —
 *           keeps the "UE owns render, SeinARTS owns sim" boundary clean.
 *
 *           Designer adds this AC to a building BP, configures visual config
 *           (mesh swap kind, decal, decal size). The sim payload
 *           `FSeinConstructionComponent` (BuildTime / Progress / CompletionEffect)
 *           is authored separately in the entity bridge's `ComponentData`
 *           array — sim and render are two co-equal authoring surfaces.
 *
 *           At BeginPlay, subscribes to the owning actor's
 *           USeinEntityComponent::OnVisualEvent delegate. When a
 *           ConstructionStateChanged event arrives, enters/exits the
 *           construction visual state — hides main meshes + spawns the
 *           configured placement visual (+ optional ground decal); restores
 *           on construction-complete.
 *
 *           Presentation counterpart to the deterministic
 *           FSeinConstructionComponent payload.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "SeinConstructionRenderComponent.generated.h"

class USeinEntityComponent;
class UStaticMesh;
class USkeletalMesh;
class UMaterialInterface;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class UMeshComponent;
class UDecalComponent;
struct FSeinVisualEvent;

/**
 * Which kind of placeholder visual is shown while under construction. Only
 * the matching slot's mesh field is editable in the details panel
 * (EditConditionHides hides the others outright).
 */
UENUM(BlueprintType)
enum class ESeinConstructionPlacementVisualType : uint8
{
	/** No placeholder — main mesh is hidden but nothing replaces it.
	 *  Useful for "ground stamp / decal only" minimal previews. */
	None,

	/** Static mesh marker — drag a UStaticMesh asset into the slot. */
	StaticMesh,

	/** Skeletal mesh — animated under-construction model (anim BPs can listen
	 *  to progress and drive a build-up sequence). */
	SkeletalMesh,

	/** Spawn a designer-authored AActor blueprint at the entity's pose.
	 *  Pick this for fancy under-construction visuals — animated meshes,
	 *  particles, custom BP logic. */
	BlueprintActor
};

UCLASS(ClassGroup = (SeinARTS),
	meta = (BlueprintSpawnableComponent, DisplayName = "SeinARTS Construction Renderer"))
class SEINARTSCOREENTITY_API USeinConstructionRenderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinConstructionRenderComponent();

	// ─── Visual config (render-side authoring) ───

	/** Which kind of placeholder visual to show while under construction.
	 *  Selecting a type reveals its dedicated slot in the panel below. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	ESeinConstructionPlacementVisualType PlacementVisualType = ESeinConstructionPlacementVisualType::None;

	/** Static-mesh placeholder. Visible only when PlacementVisualType == StaticMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (EditCondition = "PlacementVisualType == ESeinConstructionPlacementVisualType::StaticMesh", EditConditionHides))
	TObjectPtr<UStaticMesh> PlacementStaticMesh;

	/** Skeletal-mesh placeholder. Visible only when PlacementVisualType == SkeletalMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (EditCondition = "PlacementVisualType == ESeinConstructionPlacementVisualType::SkeletalMesh", EditConditionHides))
	TObjectPtr<USkeletalMesh> PlacementSkeletalMesh;

	/** AActor blueprint to spawn at the entity's pose during construction.
	 *  Visible only when PlacementVisualType == BlueprintActor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (EditCondition = "PlacementVisualType == ESeinConstructionPlacementVisualType::BlueprintActor", EditConditionHides))
	TSubclassOf<AActor> PlacementBlueprint;

	/** Optional decal material stamped on the ground beneath this entity's
	 *  footprint while it is under construction (e.g. a gravel patch showing
	 *  where construction is laid out). Composes orthogonally with
	 *  PlacementVisualType — works alongside any placeholder type or on its
	 *  own. Null = no decal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	TObjectPtr<UMaterialInterface> GroundStampDecal;

	/** World-space size of the spawned ground decal. Defaults to a ~5m x 5m
	 *  patch projected ~1m down. Designers should match this to the
	 *  building's footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	FVector GroundStampDecalSize = FVector(256.0f, 256.0f, 256.0f);

	// ─── UActorComponent ───

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Bound to the entity bridge's `OnVisualEvent` delegate. Filters by
	 *  Type=ConstructionStateChanged and Event.Value (bUnderConstruction);
	 *  drives Enter / Exit construction state. */
	UFUNCTION()
	void HandleVisualEvent(const FSeinVisualEvent& Event);

private:
	/** Hide main mesh components, spawn the configured placement visual
	 *  (+ optional decal). Idempotent — guarded by `bInConstructionState`. */
	void EnterConstructionState();

	/** Destroy spawned visuals, restore main mesh visibility. Idempotent. */
	void ExitConstructionState();

	/** Spawned placement actor, when PlacementVisualType == BlueprintActor. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> SpawnedPlacementActor;

	/** Spawned static-mesh component, when PlacementVisualType == StaticMesh. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SpawnedPlacementStaticMesh;

	/** Spawned skeletal-mesh component, when PlacementVisualType == SkeletalMesh. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> SpawnedPlacementSkeletalMesh;

	/** Spawned ground-stamp decal component, when GroundStampDecal is set. */
	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> SpawnedDecalComponent;

	/** Mesh components hidden by EnterConstructionState (the actor's "final"
	 *  visuals). Restored on ExitConstructionState. We track only the ones
	 *  we hid so designer-controlled visibility on other meshes survives the
	 *  round-trip unchanged. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UMeshComponent>> HiddenMainMeshes;

	/** Cached bridge ref so we can unsubscribe cleanly in EndPlay. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USeinEntityComponent> CachedBridge;

	bool bInConstructionState = false;
};
