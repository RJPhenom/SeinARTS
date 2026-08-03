/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinActorBridgeSubsystem.h
 * @brief   Bridges the deterministic simulation and Unreal's visual layer.
 *          Spawns/destroys actors for sim entities, syncs transforms via
 *          frame-coalesced transform capture, and routes visual events to
 *          actors each render frame.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "GameplayTagContainer.h"
#include "Events/SeinVisualEvent.h"
#include "SeinActorBridgeSubsystem.generated.h"

class ASeinActor;
class USeinWorldSubsystem;

/** Broadcast when a tech is researched (for UI refresh). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTechResearched, FSeinPlayerID, Player, FGameplayTag, TechTag);

/** Broadcast for every visual event dispatched (for global UI listeners like combat text). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVisualEventDispatched, const FSeinVisualEvent&, Event);

/** Native presentation notification after an entity's visual actor enters the bridge map. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSeinActorRegistered, FSeinEntityHandle);

/**
 * World subsystem that bridges the deterministic simulation with Unreal actors.
 *
 * Responsibilities:
 * - Listens to simulation-frame completion and captures the latest state on
 *   all managed actors once (catch-up ticks snap; single ticks interpolate)
 * - Flushes visual events each render frame and dispatches them:
 *     - EntitySpawned → spawns the Blueprint actor, calls InitializeWithEntity
 *     - EntityDestroyed → fires death events, sets actor lifespan for cleanup
 *     - All other events → routes to the target actor's HandleVisualEvent()
 * - Maintains a Handle → Actor map for O(1) lookup
 */
UCLASS()
class SEINARTSCOREENTITY_API USeinActorBridgeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// UTickableWorldSubsystem interface
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	// ========== Public API ==========

	/** Get the visual actor for a sim entity. Returns nullptr if no actor exists. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Bridge")
	ASeinActor* GetActorForEntity(FSeinEntityHandle Handle) const;

	/** Visit live render actors without walking the full sim entity pool or
	 *  repeating handle-map lookups. Presentation-only; ordering is undefined. */
	void ForEachRegisteredActor(
		TFunctionRef<void(FSeinEntityHandle, ASeinActor&)> Visitor) const;

	/** Manually register an actor for an entity (for pre-placed level actors). */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Bridge")
	void RegisterActor(FSeinEntityHandle Handle, ASeinActor* Actor);

	/** Manually unregister an actor from the bridge. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Bridge")
	void UnregisterActor(FSeinEntityHandle Handle);

	/**
	 * Materialize one already-frozen level actor into the simulation and bind
	 * its render bridge. Bootstrap planners call this in their own canonical
	 * order; no discovery or sorting occurs inside this method.
	 */
	FSeinEntityHandle RegisterPlacedActor(ASeinActor& PlacedActor);

	/** Walk every level-placed ASeinActor (stable-sorted by actor name) and
	 *  bind each one to a sim entity. Idempotent — actors already linked
	 *  are skipped. The stable sort matters for lockstep determinism: every
	 *  client must auto-register placed actors in the SAME order so their
	 *  entity IDs match the server. TActorIterator order is implementation-
	 *  defined and can vary across machines.
	 *
	 *  Normally invoked automatically from OnWorldBeginPlay. Match-flow
	 *  orchestrators (USeinMatchBootstrapSubsystem) disable that auto path
	 *  via SetAutoRegisterOnBeginPlay(false). New bootstrap code freezes a
	 *  level-qualified plan and calls RegisterPlacedActor in that order; this
	 *  convenience remains useful to non-match worlds. */
	void RegisterAllPlacedActors(UWorld& InWorld);

	/** Disable the auto-call from OnWorldBeginPlay. Call from a match-flow
	 *  orchestrator's Initialize so the orchestrator owns the bootstrap
	 *  order. Default true (back-compat for projects without an orchestrator). */
	void SetAutoRegisterOnBeginPlay(bool bInAuto) { bAutoRegisterOnBeginPlay = bInAuto; }

	/**
	 * Reconcile the render-side actor map against the current sim state.
	 * Runs a two-pass walk:
	 *
	 *   1. Cull orphans — for every (Handle, Actor) in EntityActorMap, if the
	 *      sim no longer has a live entity at Handle, mark the actor for
	 *      destruction (uses DestroyActorDelay so death anims play if any
	 *      cleanup the actor wants to run can complete).
	 *
	 *   2. Spawn missing — walk every alive sim entity; for any without an
	 *      actor in EntityActorMap, look up its class via
	 *      USeinWorldSubsystem::GetEntityActorClass and spawn one. Fires the
	 *      same OnEntitySpawned hook used by the normal sim → render path.
	 *
	 * Called by `USeinWorldSubsystem::RestoreSnapshot` after the sim's pool +
	 * component storages have been rehydrated, so the render layer matches
	 * the new sim state without orphans or headless entities. Safe to call
	 * any time — also useful after bulk sim mutations from designer code.
	 */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Bridge")
	void ReconcileBridgeAfterRestore();

	// ========== Configuration ==========

	/** Delay before destroying an actor after its entity dies (for death animations). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Bridge")
	float DestroyActorDelay = 3.0f;

	// ========== Events ==========

	/** Fired when a tech research completes (for UI systems to refresh). */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Bridge")
	FOnTechResearched OnTechResearched;

	/** Fired for every visual event before per-actor routing. Global UI systems (combat text, notifications) bind to this. */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|Bridge")
	FOnVisualEventDispatched OnVisualEventDispatched;

	/** Render-side consumers use this to update sparse actor-derived caches
	 *  without rescanning the simulation every frame. */
	FOnSeinActorRegistered OnActorRegistered;

private:
	/** Map of entity handles to their visual actors. */
	TMap<FSeinEntityHandle, TWeakObjectPtr<ASeinActor>> EntityActorMap;

	/** Cached pointer to the sim subsystem. */
	TWeakObjectPtr<USeinWorldSubsystem> SimSubsystem;

	/** When true (default), OnWorldBeginPlay fires RegisterAllPlacedActors
	 *  itself. Match-flow orchestrators flip this to false in their
	 *  Initialize so they can sequence pre-spawning and placed-actor
	 *  registration deterministically. */
	bool bAutoRegisterOnBeginPlay = true;

	/** Delegate handle for the presentation-frame callback. */
	FDelegateHandle SimFrameDelegateHandle;

	/** Called after the frame's sim pump — syncs latest transform snapshots. */
	void HandleSimFrame(int32 LatestTick, int32 TicksProcessed);

	/** Process a single visual event. */
	void DispatchVisualEvent(const FSeinVisualEvent& Event);

	/** Spawn a visual actor for an entity. */
	void SpawnActorForEntity(FSeinEntityHandle Handle, const FSeinVisualEvent& SpawnEvent);

	/** Handle entity destroyed event. */
	void HandleEntityDestroyed(FSeinEntityHandle Handle, const FSeinVisualEvent& DestroyEvent);
};
