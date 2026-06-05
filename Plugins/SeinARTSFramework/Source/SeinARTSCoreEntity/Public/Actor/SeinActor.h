/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinActor.h
 * @date:		2/28/2026
 * @author:		RJ Macklem
 * @brief:		Base actor class for simulation-backed entities.
 *				Uses generational entity handles and visual event callbacks.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/SeinEntityHandle.h"
#include "Types/FixedPoint.h"
#include "Types/Quat.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "SeinActor.generated.h"

class USeinEntityComponent;

/**
 * Base actor class for entities backed by the deterministic simulation.
 * Designers inherit from this to create unit types, buildings, projectiles, etc.
 * Provides Blueprint-friendly interface for simulation interaction and
 * visual event callbacks (damage, abilities, effects, death).
 */
UCLASS(Blueprintable, BlueprintType)
class SEINARTSCOREENTITY_API ASeinActor : public AActor
{
	GENERATED_BODY()

public:
	ASeinActor();

	// AActor interface
	virtual void BeginPlay() override;

#if WITH_EDITOR
	/** Edit-time snapshot of the actor's sim location. Fires every time
	 *  the designer drags the actor in the level editor; result is
	 *  serialized to the .umap so all clients later load the same bytes,
	 *  no per-platform FromFloat conversion at runtime. */
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** Surfaces this entity's IdentityTag — which lives nested in an
	 *  `FSeinIdentityComponent` inside the bridge's `ComponentData` array, so
	 *  `AssetRegistrySearchable` can't reach it — onto this asset's FAssetData
	 *  (key `SeinAssetTagKeys::IdentityTag`, bare `ToString()` form). Lets the
	 *  editor auto-tag collision check read it WITHOUT loading the Blueprint +
	 *  CDO. Additive: chains Super first. Harvested at save / FAssetData
	 *  construction, so the registry value is the last-saved tag. */
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	/**
	 * Initialize this actor with a simulation entity.
	 * Called automatically when spawned via USeinWorldSubsystem::SpawnEntity.
	 * @param Handle - Generational entity handle to link to
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Entity")
	virtual void InitializeWithEntity(FSeinEntityHandle Handle);

	/**
	 * Get the entity handle this actor represents.
	 * @return Generational entity handle, or invalid if not linked
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	FSeinEntityHandle GetEntityHandle() const;

	/**
	 * Check if this actor has a valid simulation entity.
	 * @return True if linked to a valid, living entity
	 */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Entity")
	bool HasValidEntity() const;

	/** Owner slot for level-placed instances (1-based; 0 = neutral). Read by
	 *  `USeinActorBridgeSubsystem::OnWorldBeginPlay` and stamped onto the
	 *  spawned sim entity. EditInstanceOnly because ownership is per-placement
	 *  — BP class defaults must not carry an owner. Slot value maps directly
	 *  to `FSeinPlayerID(slot)`, matching the convention in
	 *  `ASeinGameMode::HandleStartingNewPlayer_Implementation`. Faction/team
	 *  are derived from the owning player's state, not stored per-entity. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "SeinARTS|Ownership", meta = (ClampMin = "0", ClampMax = "8"))
	int32 PlayerSlot = 0;

	/** Editor-baked snapshot of this actor's sim location, computed at edit
	 *  time via `FFixedVector::FromVector(GetActorLocation())` whenever the
	 *  actor moves in the editor (`PostEditMove`). Auto-registered placed
	 *  actors (`USeinActorBridgeSubsystem::OnWorldBeginPlay`) read this
	 *  value at spawn time instead of the actor's float transform — the
	 *  conversion happens once in the editor process and the resulting
	 *  FFixedVector is serialized to the .umap, so all client platforms
	 *  load identical bytes regardless of CPU arch / compiler / FPU mode.
	 *  This is what makes lockstep cross-platform safe (PC ↔ ARM Mac ↔
	 *  mobile ↔ console). `bSimLocationBaked` flips true on first
	 *  PostEditMove — runtime warns + falls back to FromFloat if a placed
	 *  actor's snapshot is stale. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedSimLocation = FFixedVector::ZeroVector;

	/** True once `PlacedSimLocation` has been baked from the actor's
	 *  current world transform. Catches "actor placed before this property
	 *  existed" — runtime checks this to know whether to trust the
	 *  snapshot or warn the designer + fall back. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	bool bSimLocationBaked = false;

	/** Editor-baked snapshot of this actor's sim rotation. Parallel to
	 *  `PlacedSimLocation` — same bake-on-PostEditMove pipeline, same
	 *  cross-arch determinism story (FromQuat at edit time, identical
	 *  int64 bits serialized to .umap, no float conversion at runtime).
	 *
	 *  Required so placed actors with editor-set rotation (cover-providing
	 *  sandbag walls placed at 45°, oriented foxholes, rotated buildings,
	 *  etc.) retain their rotation in PIE. Without this the sim entity's
	 *  transform was constructed with identity rotation and the actor
	 *  bridge re-applied identity to the AActor every frame, snapping it
	 *  to yaw=0 visually + breaking cover-slot world positions. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedQuaternion PlacedSimRotation = FFixedQuaternion::Identity;

	/** True once `PlacedSimRotation` has been baked. Mirrors
	 *  `bSimLocationBaked` — runtime warns + falls back to FromQuat if
	 *  not set (legacy placed actors from before the rotation bake
	 *  existed). Designer re-saves the level (or nudges the actor) to
	 *  bake. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	bool bSimRotationBaked = false;

protected:
	/** The single actor component linking this actor to its sim entity AND
	 *  carrying the authored sim-data array. Replaces the prior
	 *  legacy ActorBridge / ArchetypeDefinition / TagsComponent split. Class is
	 *  `USeinEntityComponent`, displayed as "SeinARTS Entity Bridge" in the
	 *  components panel. Variable + subobject names: `EntityBridge`. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS",
		meta = (DisplayName = "SeinARTS Entity Bridge"))
	TObjectPtr<USeinEntityComponent> EntityBridge;

public:
	// -- Lifecycle events --

	/** Called when entity is initialized. Override in Blueprints for custom setup. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Entity Initialized"))
	void ReceiveEntityInitialized();

	/** Called when entity is destroyed in simulation. Override for cleanup / death effects. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Entity Destroyed"))
	void ReceiveEntityDestroyed();

	// -- Visual events from simulation --
	// These are fired by USeinEntityComponent::HandleVisualEvent and are
	// BlueprintImplementableEvent so designers can react in BP subclasses.

	/** Called when this entity takes damage (DESIGN §11 DamageApplied event). */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Damage Applied"))
	void ReceiveDamageApplied(FFixedPoint Amount, FSeinEntityHandle Source, FGameplayTag DamageType);

	/** Called when this entity is healed (DESIGN §11 HealApplied event). */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Heal Applied"))
	void ReceiveHealApplied(FFixedPoint Amount, FSeinEntityHandle Source, FGameplayTag HealType);

	/** Called when this entity lands a kill on another. */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Kill"))
	void ReceiveKill(FSeinEntityHandle Killed);

	/** Called when an ability is activated on this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Ability Activated"))
	void ReceiveAbilityActivated(FGameplayTag AbilityTag);

	/** Called when an ability ends on this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Ability Ended"))
	void ReceiveAbilityEnded(FGameplayTag AbilityTag);

	/** Called when a gameplay effect is applied to this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Effect Applied"))
	void ReceiveEffectApplied(FGameplayTag EffectTag);

	/** Called when a gameplay effect is removed from this entity */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Effect Removed"))
	void ReceiveEffectRemoved(FGameplayTag EffectTag);

	/** Called when this entity dies */
	UFUNCTION(BlueprintImplementableEvent, Category = "SeinARTS|Events", meta = (DisplayName = "On Death"))
	void ReceiveDeath();
};
