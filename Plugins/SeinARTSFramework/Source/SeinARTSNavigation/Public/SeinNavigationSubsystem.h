/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationSubsystem.h
 * @brief   Thin world subsystem that owns the active USeinNavigation instance.
 *
 *          Reads `USeinARTSCoreSettings::NavigationClass` on Initialize, new's
 *          up that class, and re-exposes it to the rest of the engine (move-to
 *          action, ability validation delegate). Registers the nav as the "Nav"
 *          layer provider on the unified level-data substrate and adopts the
 *          baked grid from it ("Bake Level Data" lives on ASeinLevelVolume).
 *
 *          The subsystem does NOT know what a "grid" or "navmesh" is — it only
 *          knows a USeinNavigation exists. All nav semantics live on the active
 *          subclass.
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SeinPathTypes.h"
#include "Core/SeinEntityHandle.h"
#include "SeinNavigationSubsystem.generated.h"

class USeinNavigation;
class USeinLevelData;
class ISeinSystem;
struct FSeinPathRequest;
struct FSeinPath;

UCLASS()
class SEINARTSNAVIGATION_API USeinNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** The active navigation instance. Never null after Initialize. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation")
	USeinNavigation* GetNavigation() const { return Navigation; }

	/** Convenience accessor for BP and external callers that only have a
	 *  UObject-with-world. Returns null if the world has no nav subsystem. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (WorldContext = "WorldContextObject"))
	static USeinNavigation* GetNavigationForWorld(const UObject* WorldContextObject);

	/** Budgeted path request. Routes to `Navigation->FindPath` when the
	 *  per-sim-tick request budget (configured via
	 *  `USeinARTSCoreSettings::PathRequestsPerTickBudget`) hasn't been spent
	 *  yet; otherwise returns `Throttled` without doing any A* work — caller
	 *  retries next tick. Counter resets on every
	 *  `USeinWorldSubsystem::OnSimTickCompleted`.
	 *
	 *  Lockstep-deterministic: all clients increment the counter in the same
	 *  order (latent-action tick order) so all clients agree which requests
	 *  are throttled in a given tick.
	 *
	 *  Use this from any tickable consumer (latent move actions, abilities)
	 *  that can wait a tick for a path. One-shot callers (BPFL queries,
	 *  reachability checks) should call `Navigation->FindPath` directly to
	 *  bypass the budget. */
	ESeinPathResult RequestPath(const FSeinPathRequest& Request, FSeinPath& OutPath);

private:

	/** The active nav for this world. Instantiated from
	 *  `USeinARTSCoreSettings::NavigationClass` during Initialize. */
	UPROPERTY(Transient)
	TObjectPtr<USeinNavigation> Navigation;

	/** The shared level-data substrate (CP1.1), resolved in Initialize. Nav
	 *  registers as its "Nav" layer provider and loads its runtime grid from the
	 *  baked channels when present. Weak — owned by USeinLevelDataSubsystem. */
	TWeakObjectPtr<USeinLevelData> LevelData;

	/** Handle for our USeinLevelData::OnLevelDataMutated subscription; removed at
	 *  Deinitialize. */
	FDelegateHandle LevelDataMutatedHandle;

	/** Called in OnWorldBeginPlay — adopts the unified level-data substrate's
	 *  baked grid into the nav (when it carries a "Nav" channel). Idempotent. */
	void LoadBakedAssetIntoNav(UWorld& World);

	/** Re-adopt the shared substrate's grid when it rebakes / reloads (CP1.1).
	 *  Bound to USeinLevelData::OnLevelDataMutated. No-op if nav doesn't read the
	 *  substrate or the substrate has no nav data. */
	void OnLevelDataChanged();

	/** Binds cross-module delegates on USeinWorldSubsystem so sim code can
	 *  query nav reachability without importing nav headers. */
	void BindSimDelegates(UWorld& World);

	/** Sim-tick system that gathers FSeinExtentsComponent entities (those with
	 *  bBlocksNav set) each PreTick
	 *  and pushes them into Navigation->SetDynamicBlockers. Owned here so
	 *  Navigation stays a pure data/query object — the world subsystem just
	 *  ticks it. Created at OnWorldBeginPlay (when SeinWorldSubsystem is
	 *  available); torn down at Deinitialize. */
	ISeinSystem* NavBlockerStampSystem = nullptr;

	/** Number of `RequestPath` calls served (Found or NotFound) within the
	 *  current sim tick. Reset at the top of `RequestPath` whenever the
	 *  current sim tick differs from `LastResetTick` — a self-checking
	 *  scheme that's bulletproof against subscription failures (live
	 *  coding patches, world reload races, etc.) that an `OnSimTickCompleted`
	 *  binding could miss. Throttled requests do NOT consume budget — they
	 *  short-circuit before `Navigation->FindPath` runs. */
	int32 PathRequestsThisTick = 0;

	/** Sim tick at which `PathRequestsThisTick` was last reset. -1 = never
	 *  reset. Compared on every `RequestPath` against the current sim tick;
	 *  mismatch triggers a fresh-tick reset. See note above on why this
	 *  replaces the old `OnSimTickCompleted` subscription. */
	int32 LastResetTick = -1;

	// ── Async pathfinding (Sein.Sim.AsyncPathfinding) ─────────────────────────
	// Opt-in: requests queue here keyed by Requester (re-requests dedup) and run as
	// a deterministic parallel batch one tick later via Navigation->RunPathBatch.
	// See RequestPath / DrainAsyncPathQueue.

	/** Pending requests, keyed by Requester (latest request per unit wins). */
	TMap<FSeinEntityHandle, FSeinPathRequest> AsyncQueue;

	/** Ready results from the last drain, consumed on the requester's next RequestPath. */
	TMap<FSeinEntityHandle, FSeinPath> AsyncResults;

	/** Sim tick the async queue was last drained (drain runs once per tick). */
	int32 LastDrainTick = -1;

	/** Drain the async queue: serve up to PathRequestsPerTickBudget requests in
	 *  canonical handle order via Navigation->RunPathBatch, caching the results.
	 *  Runs once per tick at the first async RequestPath of that tick. */
	void DrainAsyncPathQueue(int32 CurrentTick);
};
