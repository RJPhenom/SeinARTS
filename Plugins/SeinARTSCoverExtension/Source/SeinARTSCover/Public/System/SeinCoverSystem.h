/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinCoverSystem.h
 * @brief   Abstract base for pluggable cover-query implementations.
 *
 *          USeinCoverSystem owns the cover problem for one world: provider
 *          registration, spatial bookkeeping, and the QueryCoverAt API. It is
 *          the ONLY thing the framework's preview decals, BPFL queries, and
 *          (eventually) cover-aware formation solver talk to.
 *
 *          Configured via plugin settings (`USeinARTSCoreSettings::CoverSystemClass`).
 *          The framework ships `USeinCoverDefault` as a minimal flat-list +
 *          per-query spatial check reference impl; game teams can subclass
 *          (spatial hash, tile-based, cached-per-tick) without touching the
 *          rest of the framework.
 *
 *          Decoupling contract:
 *          - The preview subsystem, BPFL, and per-entity cover state system
 *            call into this class's virtual surface only.
 *          - Concrete subclasses own their own provider index storage.
 *          - Provider data lives on entities (FSeinCoverComponent); the
 *            cover system just keeps a list of provider entity handles.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "GameplayTagContainer.h"
#include "Types/SeinCoverTypes.h"
#include "SeinCoverSystem.generated.h"

class USeinWorldSubsystem;

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Cover System"))
class SEINARTSCOVER_API USeinCoverSystem : public UObject
{
	GENERATED_BODY()

public:
	// ----------------------------------------------------------------------
	// Lifecycle — called by USeinCoverSubsystem
	// ----------------------------------------------------------------------

	/** Called when the cover subsystem activates this system. Default: stores
	 *  the world ref. Subclasses override to set up spatial indexes, bind to
	 *  external events, etc. Always call Super. */
	virtual void OnCoverSystemInitialized(USeinWorldSubsystem* InWorld);

	/** Called on final teardown. Default: clears the world ref and the base
	 *  registry mirror. Subclasses override to release index storage and
	 *  external bindings. Always call Super. */
	virtual void OnCoverSystemDeinitialized();

	// ----------------------------------------------------------------------
	// Provider registration
	// ----------------------------------------------------------------------

	/**
	 * Canonical front door used by USeinCoverSubsystem when a provider enters
	 * the authoritative world. It de-duplicates handles and keeps registration
	 * order canonical even when an entity slot is reused.
	 */
	void RegisterAuthoritativeProvider(FSeinEntityHandle ProviderHandle);

	/**
	 * Canonical front door used by USeinCoverSubsystem when a provider leaves
	 * the authoritative world. Safe to call for an already-absent handle.
	 */
	void UnregisterAuthoritativeProvider(FSeinEntityHandle ProviderHandle);

	/** Implementation hook for adding a cover-providing entity after the base
	 *  front door has normalized registry history.
	 *
	 *  Default: no-op. Subclasses MUST override to track the handle. */
	virtual void RegisterProvider(FSeinEntityHandle ProviderHandle) PURE_VIRTUAL(USeinCoverSystem::RegisterProvider, );

	/** Implementation hook for removing a cover-providing entity. It must be
	 *  idempotent because reconciliation may remove a stale handle. */
	virtual void UnregisterProvider(FSeinEntityHandle ProviderHandle) PURE_VIRTUAL(USeinCoverSystem::UnregisterProvider, );

	/**
	 * Replace all derived provider bookkeeping from a canonical, handle-sorted
	 * view of the authoritative world. Called after snapshot/resync adoption
	 * and when the subsystem first attaches to an already-populated world.
	 *
	 * The base owns this operation: it unregisters every previously mirrored
	 * provider and replays the existing RegisterProvider/UnregisterProvider
	 * implementation hooks in canonical order. A custom implementation does
	 * not need a new restore API and its lifecycle methods remain activation /
	 * final-teardown only.
	 */
	void RebuildProviderRegistry(
		const TArray<FSeinEntityHandle>& ProviderHandles);

	// ----------------------------------------------------------------------
	// Queries
	// ----------------------------------------------------------------------

	/** Returns every cover context active at the given world point.
	 *
	 *  A unit standing at this point would be subject to ALL the returned
	 *  contexts simultaneously — combat scripts iterate and evaluate each
	 *  independently (directional contexts check shot incoming direction,
	 *  area contexts always apply).
	 *
	 *  When `Observer` is valid, results are filtered to cover providers
	 *  visible to that observer per the FoW visibility policy (cover the
	 *  player can't see doesn't influence their preview / snap / etc.).
	 *  When `Observer` is invalid (default), all registered providers are
	 *  considered — appropriate for combat scripts that need the full
	 *  ground truth (the cover physically exists regardless of who sees it).
	 *
	 *  Empty array = no cover at this point. Order is implementation-defined
	 *  and SHOULD NOT be relied on by callers (callers needing a "best"
	 *  result should use `QueryBestCoverQualityAt` or do their own selection
	 *  against the array).
	 *
	 *  Default: empty array. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Cover")
	virtual TArray<FSeinCoverContext> QueryCoverAt(FFixedVector WorldPoint,
		FSeinPlayerID Observer = FSeinPlayerID()) const;

	/** Convenience: returns the strongest cover quality tag among the active
	 *  contexts at the given point, or an invalid tag when there's no cover.
	 *  Used by the formation preview to pick a single color per decal.
	 *
	 *  `Observer` filtering matches `QueryCoverAt`: when valid, only cover
	 *  the observer can see is considered. Default invalid = no filtering.
	 *
	 *  "Strongest" is determined by the implementation. Default impl returns
	 *  the first context's tag (arbitrary). Subclasses can override to apply
	 *  designer-configured ordering (heavy > light > neutral > negative). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Cover")
	virtual FGameplayTag QueryBestCoverQualityAt(FFixedVector WorldPoint,
		FSeinPlayerID Observer = FSeinPlayerID()) const;

	/** Returns every slot candidate within `Radius` (world units) of `Origin`,
	 *  resolved to world space via each provider's actor transform. Used by
	 *  cover-aware broker resolvers to snap eligible squad members to cover
	 *  when a move order targets a position near cover providers. Result
	 *  ordering is implementation-defined; the default impl returns them
	 *  sorted by ascending distance from Origin for greedy nearest-first
	 *  allocation by the caller.
	 *
	 *  When `Observer` is valid, only slots from cover providers visible to
	 *  that observer are returned (per-player snap respects fog). Snap
	 *  callers pass the order-issuing player; combat scripts that need raw
	 *  geometry pass invalid (default).
	 *
	 *  Slot-based (directional) cover only — area cover doesn't contribute
	 *  candidates because there are no discrete positions to snap to. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SeinARTS|Cover")
	virtual TArray<FSeinCoverSlotCandidate> FindNearbySlots(FFixedVector Origin, FFixedPoint Radius,
		FSeinPlayerID Observer = FSeinPlayerID()) const;

protected:
	/** World ref cached on Initialize. Subclasses use this to look up entity
	 *  components, transforms, etc. during queries. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USeinWorldSubsystem> World;

private:
	/** Sorted mirror of the handles delivered through the authoritative front
	 *  door. Derived query/index state is still owned by the subclass. */
	TArray<FSeinEntityHandle> AuthoritativeProviderHandles;
};
