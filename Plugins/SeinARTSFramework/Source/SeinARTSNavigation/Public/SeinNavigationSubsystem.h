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
#include "SeinStaticEnvironmentAdoption.h"
#include "Core/SeinEntityHandle.h"
#include "SeinNavigationSubsystem.generated.h"

class USeinNavigation;
class USeinLevelData;
class USeinLevelDataSubsystem;
class ISeinSystem;
class FSeinNavBlockerStampSystem;
struct FSeinNavigationCanonicalStateProvider;
struct FSeinNavigationRoutineRootCache;
struct FSeinNavigationRestoreStage;
struct FSeinPathRequest;
struct FSeinPath;

#if WITH_DEV_AUTOMATION_TESTS
namespace UE::SeinARTSTests
{
	struct FNavigationCanonicalStateTestAccess;
}
#endif

UCLASS()
class SEINARTSNAVIGATION_API USeinNavigationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/**
	 * Unregister systems and delegates, then release module-owned runtime state
	 * while SeinARTSNavigation code is still loaded. Called by the module's
	 * PreUnloadCallback; idempotent with later world teardown.
	 */
	void ReleaseModuleOwnedStateForModuleUnload();

	/** The active navigation instance. Null when NavigationClass is None (navigation off, WYSIWYG);
	 *  otherwise valid after Initialize. All callers null-guard. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation")
	USeinNavigation* GetNavigation() const { return Navigation; }

	/** Convenience accessor for BP and external callers that only have a
	 *  UObject-with-world. Returns null if the world has no nav subsystem. */
	UFUNCTION(BlueprintPure, Category = "SeinARTS|Navigation", meta = (WorldContext = "WorldContextObject"))
	static USeinNavigation* GetNavigationForWorld(const UObject* WorldContextObject);

	/** True once initial Level Data adoption resolved to Adopted or the valid
	 *  NotApplicable fallback. False while pending or after rejection. */
	bool IsInitialStaticEnvironmentPrepared() const
	{
		return bInitialStaticEnvironmentPrepared;
	}

	/** Exact result of the latest initial/static-environment adoption attempt. */
	const FSeinStaticEnvironmentAdoptionResult&
	GetInitialStaticEnvironmentAdoptionResult() const
	{
		return InitialStaticEnvironmentAdoptionResult;
	}

	/** Budgeted path request. Routes to `Navigation->FindPath` when the
	 *  per-sim-tick request budget (configured via
	 *  `USeinARTSCoreSettings::PathRequestsPerTickBudget`) hasn't been spent
	 *  yet; otherwise returns `Throttled` without doing any A* work — caller
	 *  retries next tick. The synchronous counter resets lazily when the
	 *  subsystem observes a new simulation tick.
	 *
	 *  Lockstep-deterministic: all clients increment the counter in the same
	 *  order (latent-action tick order) so all clients agree which requests
	 *  are throttled in a given tick.
	 *
	 *  Use this from any tickable consumer (latent move actions, abilities)
	 *  that can wait a tick for a path. One-shot callers (BPFL queries,
	 *  reachability checks) should call `Navigation->FindPath` directly to
	 *  bypass the budget. Async continuations remain owned by the requester
	 *  until they are consumed, superseded, or explicitly cancelled; custom
	 *  consumers must call CancelPathRequest when they terminate without
	 *  consuming their result. */
	ESeinPathResult RequestPath(const FSeinPathRequest& Request, FSeinPath& OutPath);

	/** Cancel all queued and ready async path state owned by Requester.
	 *  Safe and idempotent when async pathfinding is disabled or no request is
	 *  pending. Terminal tickable consumers must call this so abandoned
	 *  continuations do not remain part of canonical simulation state. */
	void CancelPathRequest(FSeinEntityHandle Requester);

private:
	friend struct FSeinNavigationCanonicalStateProvider;
	friend struct FSeinNavigationRestoreStage;
	friend class FSeinNavBlockerStampSystem;
#if WITH_DEV_AUTOMATION_TESTS
	friend struct UE::SeinARTSTests::FNavigationCanonicalStateTestAccess;
#endif

	/** The active nav for this world. Instantiated from
	 *  `USeinARTSCoreSettings::NavigationClass` during Initialize. */
	UPROPERTY(Transient)
	TObjectPtr<USeinNavigation> Navigation;

	/** The shared level-data substrate (CP1.1), resolved in Initialize. Nav
	 *  registers as its "Nav" layer provider and loads its runtime grid from the
	 *  baked channels when present. Weak — owned by USeinLevelDataSubsystem. */
	TWeakObjectPtr<USeinLevelData> LevelData;
	TWeakObjectPtr<USeinLevelDataSubsystem> LevelDataSubsystem;

	/** Handle for our USeinLevelData::OnLevelDataMutated subscription; removed at
	 *  Deinitialize. */
	FDelegateHandle LevelDataMutatedHandle;
	FDelegateHandle InitialLevelDataPreparedHandle;
	FDelegateHandle EntitySpawnedProfileWarmHandle;

	/** Exact static navigation identity frozen into the match StateContract. */
	bool bNavigationConfigured = false;
	bool bInitialStaticEnvironmentPrepared = false;
	bool bStateBindingFrozen = false;
	FSeinStaticEnvironmentAdoptionResult
		InitialStaticEnvironmentAdoptionResult;
	FString ConfiguredNavigationClassPath;
	FString StateBindingFailureReason;
	FString FrozenStateBindingFrame;
	FGuid FrozenStaticEnvironmentDigest;
	/** Implementation mutation counter latched at freeze; per-tick recapture
	 *  fail-stops on drift. Local tamper-evidence only — never peer-compared. */
	uint64 FrozenStaticEnvironmentGeneration = 0;

	/** Adopt the unified level-data substrate's baked grid into the nav when
	 *  initial level-data preparation completes. Idempotent. */
	FSeinStaticEnvironmentAdoptionResult LoadBakedAssetIntoNav(
		UWorld& World);
	void HandleInitialLevelDataPrepared();
	bool PrepareInitialCanonicalStateEnvironment(FString& OutError);

	/** Re-adopt the shared substrate's grid when it rebakes / reloads (CP1.1).
	 *  Bound to USeinLevelData::OnLevelDataMutated. Empty Level Data and
	 *  non-participating navs retain their valid fallback; rejected data clears
	 *  readiness until a corrected bake is adopted. */
	void OnLevelDataChanged();
	void WarmExistingAgentProfiles();
	void HandleEntitySpawnedForProfileWarm(
		FSeinEntityHandle Handle);

	/** Provider-only exact implementation/static-environment contract freeze.
	 *  A provisional restore declaration never persists the candidate. */
	bool FreezeCanonicalStateBinding(
		bool bCommit,
		FString& OutFrame,
		FGuid& OutStaticDigest,
		FString& OutError);

	bool RevalidateCanonicalStateBindingCandidate(
		const FString& ExpectedFrame,
		const FGuid& ExpectedStaticDigest,
		FString& OutError);
	void CommitCanonicalStateBinding(
		const FString& Frame,
		const FGuid& StaticDigest);
	bool ValidateCommittedCanonicalStateBinding();

	/** Permanently fail subsequent canonical capture after an unguarded drift. */
	void InvalidateCanonicalStateBinding(const FString& Reason);
	void InvalidateCommittedCanonicalStateBinding(const FString& Reason);

	/** Binds cross-module delegates on USeinWorldSubsystem so sim code can
	 *  query nav reachability without importing nav headers. */
	void BindSimDelegates(UWorld& World);
	void UnbindSimDelegates();

	/** Sim-tick system that gathers FSeinExtentsPayload entities (those with
	 *  bBlocksNav set) each PreTick
	 *  and pushes them into Navigation->SetDynamicBlockers. Owned here so
	 *  Navigation stays a pure data/query object — the world subsystem just
	 *  ticks it. Created and registered during Initialize, before execution
	 *  topology freeze; torn down at Deinitialize. */
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
	// deterministic budgeted batches beginning one tick later via
	// Navigation->RunPathBatch.
	// See RequestPath / DrainAsyncPathQueue.

	/** Pending requests, keyed by Requester (latest request per unit wins). */
	TMap<FSeinEntityHandle, FSeinPathRequest> AsyncQueue;

	/** A drained result paired with the request that produced it. The request identity is
	 *  re-checked on delivery so a unit re-ordered to a NEW destination since it queued never
	 *  consumes the stale path — handle-only keying would otherwise hand back the OLD goal. */
	struct FSeinAsyncPathResult
	{
		FSeinPathRequest Request;
		FSeinPath Path;
	};

	/** Ready results retained until the requester consumes, supersedes, or
	 *  explicitly cancels them. */
	TMap<FSeinEntityHandle, FSeinAsyncPathResult> AsyncResults;

	/** Sim tick the async queue was last drained (drain runs once per tick). */
	int32 LastDrainTick = -1;

	/** Cache-only write evidence for the async continuation contributor. */
	uint64 CanonicalStateMutationRevision = 0;
	mutable TSharedPtr<FSeinNavigationRoutineRootCache> RoutineRootCache;
	void MarkCanonicalStateDirty();

	/** Drain the async queue: serve up to PathRequestsPerTickBudget requests in
	 *  canonical handle order via Navigation->RunPathBatch, caching the results
	 *  and retaining the unserved budget tail. Runs once per tick at the first
	 *  async RequestPath of that tick. */
	void DrainAsyncPathQueue();

	/** True if two path requests would resolve to the SAME route: every path-affecting field
	 *  matches EXCEPT Start (re-sampled to the unit's live position on every repath, so a
	 *  Start-inclusive check would never match a moving unit) and Requester (the map key). Rejects
	 *  a cached async result whose destination/params no longer match the live request. */
	static bool PathRequestIdentityMatches(const FSeinPathRequest& A, const FSeinPathRequest& B);
};
