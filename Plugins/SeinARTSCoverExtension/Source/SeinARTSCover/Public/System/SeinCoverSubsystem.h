/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverSubsystem.h
 * @brief   Thin world subsystem that owns the active USeinCoverSystem instance.
 *
 *          Reads `USeinARTSCoreSettings::CoverSystemClass` on Initialize, new's
 *          up that class, and exposes it to consumers (preview decals, BPFL,
 *          per-entity cover state).
 *
 *          The subsystem does NOT know what a "slot" or "area" is — it only
 *          knows a USeinCoverSystem exists. All cover semantics live on the
 *          active subclass.
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/SeinEntityHandle.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinCoverSubsystem.generated.h"

class USeinCoverSystem;
class USeinWorldSubsystem;

UCLASS()
class SEINARTSCOVER_API USeinCoverSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/**
	 * Sever delegates and release the active cover implementation before its
	 * defining module unloads. Idempotent with ordinary world teardown.
	 */
	void ReleaseModuleOwnedStateForModuleUnload();

	/** The active cover system instance. Never null after Initialize completes
	 *  successfully — falls back to USeinCoverDefault when settings is empty
	 *  or its configured class fails to load. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Cover")
	USeinCoverSystem* GetCoverSystem() const { return CoverSystem; }

	/** Convenience accessor for BP and external callers that only have a
	 *  UObject-with-world. Returns null if the world has no cover subsystem
	 *  (cover module compiled in but no world yet). */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Cover", meta = (WorldContext = "WorldContextObject"))
	static USeinCoverSystem* GetCoverSystemForWorld(const UObject* WorldContextObject);

private:
	UPROPERTY(Transient)
	TObjectPtr<USeinCoverSystem> CoverSystem;

	/** Bind to USeinWorldSubsystem's lifecycle and restore delegates. Initialize
	 *  declares the Core world dependency; OnWorldBeginPlay remains a defensive
	 *  idempotent retry for unusual world construction. */
	void HookSimWorldEvents();

	/** Called by the sim world subsystem when an entity finishes spawning.
	 *  If the entity has FSeinCoverComponent in component storage, register
	 *  it with the active cover system. */
	void HandleEntitySpawned(FSeinEntityHandle Handle);

	/** Symmetric: unregister cover providers as they're destroyed. The registry
	 *  is keyed by handle, so teardown is idempotent and needs no mutable access
	 *  to the dying component payload. */
	void HandleEntityDestroyed(FSeinEntityHandle Handle);

	/** Rebuild the active implementation's derived provider registry from the
	 *  authoritative alive-entity/component view. Used after snapshot adoption
	 *  and when binding to an already-populated sim world. */
	void ReconcileProviderRegistry();

	/** Cached sim subsystem ref — set on HookSimWorldEvents. Used to inspect
	 *  components during spawn registration; destroy unregistration is handle-only. */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> CachedSimWorld;

	FDelegateHandle SpawnedHandle;
	FDelegateHandle DestroyedHandle;
	FDelegateHandle RestoredHandle;
};
