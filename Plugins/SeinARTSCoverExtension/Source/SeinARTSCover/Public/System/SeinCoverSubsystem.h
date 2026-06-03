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

	/** Bind to USeinWorldSubsystem's OnEntitySpawned/Destroyed at world ready.
	 *  The world subsystem may not be available at our Initialize() call
	 *  (UE doesn't enforce subsystem init order between modules), so we hook
	 *  via OnWorldBeginPlay as well — same pattern other systems use. */
	void HookSimWorldEvents();

	/** Called by the sim world subsystem when an entity finishes spawning.
	 *  If the entity has FSeinCoverComponent in component storage, register
	 *  it with the active cover system. */
	void HandleEntitySpawned(FSeinEntityHandle Handle);

	/** Symmetric: unregister cover providers as they're destroyed. The cover
	 *  system reads the entity's FSeinCoverComponent for its `Reach` cache
	 *  during register, so we MUST run this before component storage wipes
	 *  (matches the OnEntityDestroyed event's "before wipe" contract). */
	void HandleEntityDestroyed(FSeinEntityHandle Handle);

	/** Cached sim subsystem ref — set on HookSimWorldEvents. Used to read
	 *  components during the spawn/destroy callbacks. */
	UPROPERTY(Transient)
	TObjectPtr<USeinWorldSubsystem> CachedSimWorld;

	FDelegateHandle SpawnedHandle;
	FDelegateHandle DestroyedHandle;
};
