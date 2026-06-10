/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWar.h
 * @brief   Abstract base class for pluggable fog-of-war implementations.
 *
 *          USeinFogOfWar owns the end-to-end vision problem for one world:
 *          runtime source/blocker registration, per-player VisionGroup grid
 *          state, stamp-delta recomputation, and visibility queries. It is
 *          the ONLY thing the framework's reader BPFL and cross-module LOS
 *          delegate talk to. Baked data lives on the unified level-data
 *          substrate (USeinLevelData): a participating fog registers an
 *          ISeinLevelLayerProvider for the shared bake and loads its runtime
 *          grid back via LoadFromSubstrate.
 *
 *          Configured via plugin settings (`USeinARTSCoreSettings::FogOfWarClass`).
 *          The framework ships `USeinFogOfWarDefault` as a minimal 2D-grid +
 *          symmetric shadowcasting reference implementation; game teams can
 *          subclass or replace it entirely without touching any other
 *          framework code.
 *
 *          Decoupling contract (mirrors USeinNavigation):
 *          - The reader BPFL and cross-module LOS delegate call into this
 *            class's virtual surface only.
 *          - Concrete subclasses own their own runtime data storage.
 *          - Fog-of-war never talks to navigation. Both are LAYERS of the
 *            unified level-data substrate (CP1.1, Decisions D12): each
 *            registers an ISeinLevelLayerProvider with USeinLevelData,
 *            contributes its channel at the ONE shared bake, and loads its
 *            runtime grid from the baked channels + shared height. The
 *            sharing is mediated entirely by the substrate — nav and FoW
 *            still share no direct data or code, and a project may ship its
 *            own nav, its own fog, or both (the opt-in hooks below default
 *            to "doesn't participate").
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "Core/SeinEntityHandle.h"
#include "Core/SeinPlayerID.h"
#include "SeinFogOfWarTypes.h"
#include "SeinFogOfWar.generated.h"

class UWorld;
class USeinWorldSubsystem;
class ISeinLevelLayerProvider;
class USeinLevelData;

/** Fired when the fog-of-war's baked or runtime state mutates (bake
 *  finished, substrate adoption, dynamic blocker change). Debug viz + cached
 *  UI reads re-query on this signal. */
DECLARE_MULTICAST_DELEGATE(FSeinOnFogOfWarMutated);

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Fog Of War"))
class SEINARTSFOGOFWAR_API USeinFogOfWar : public UObject
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Lifecycle — called by USeinFogOfWarSubsystem
	// ----------------------------------------------------------------------

	/** Called once when the subsystem instantiates this impl. Default: no-op. */
	virtual void OnFogOfWarInitialized(UWorld* World) {}

	/** Called once when the subsystem is tearing down this impl. Default: no-op. */
	virtual void OnFogOfWarDeinitialized() {}

	// ----------------------------------------------------------------------
	// Runtime data state
	// ----------------------------------------------------------------------

	/** True if the impl has usable runtime data (grid dims populated, baked
	 *  substrate channel loaded or grid procedurally initialized). Queries
	 *  return no-visibility when false. Default: false — subclasses report
	 *  their own grid state. */
	virtual bool HasRuntimeData() const { return false; }

	// ----------------------------------------------------------------------
	// Unified level-data participation (CP1.1, Decisions D12) — OPT-IN. A fog
	// impl that runs as a layer provider on the shared substrate (USeinLevelData)
	// returns its provider face here and loads its runtime grid from the baked
	// channels + shared height. These hooks default to "no" so the base stays
	// agnostic of the substrate (a custom FogOfWarClass that doesn't participate
	// keeps its own bake/load story).
	// ----------------------------------------------------------------------

	/** This fog's layer-provider face, or null if it contributes no channel to the
	 *  unified level-data bake. The fog subsystem registers a non-null result so
	 *  the shared bake runs this fog's BakeLayer. Default: null (doesn't participate). */
	virtual ISeinLevelLayerProvider* GetLevelDataProvider() { return nullptr; }

	/** Load the runtime grid from the unified substrate's baked "FogOfWar" channel
	 *  + shared coordinate space. Return false if this fog doesn't read the
	 *  substrate (or the substrate carries no fog channel). Default: false. */
	virtual bool LoadFromSubstrate(const USeinLevelData& /*Substrate*/) { return false; }

	// ----------------------------------------------------------------------
	// Vision source management — sim-side, called by the actor bridge /
	// subsystem as entities spawn, move, change props, or despawn.
	// ----------------------------------------------------------------------

	/** Register a new vision source. Impl is responsible for stamping on the
	 *  next tick. Safe to call with a handle that was never registered (no
	 *  double-register). */
	virtual void RegisterSource(FSeinEntityHandle Entity, const FSeinVisionSourceParams& Params) {}

	/** Update an already-registered source (position/radius/owner changed).
	 *  Impl should diff against prior state + apply refcount deltas only to
	 *  affected cells. No-op if the entity isn't registered. */
	virtual void UpdateSource(FSeinEntityHandle Entity, const FSeinVisionSourceParams& Params) {}

	/** Unregister a source + roll back its last stamp footprint. No-op if the
	 *  entity isn't registered. */
	virtual void UnregisterSource(FSeinEntityHandle Entity) {}

	// ----------------------------------------------------------------------
	// Dynamic blocker management — runtime only. Static blockers bake into
	// the asset and aren't driven through this surface.
	// ----------------------------------------------------------------------

	virtual void RegisterBlocker(FSeinEntityHandle Entity, const FSeinVisionBlockerParams& Params) {}
	virtual void UpdateBlocker(FSeinEntityHandle Entity, const FSeinVisionBlockerParams& Params) {}
	virtual void UnregisterBlocker(FSeinEntityHandle Entity) {}

	// ----------------------------------------------------------------------
	// Runtime grid initialization — called by the subsystem on OnWorldBeginPlay
	// when no baked level data is available. Impls that require a bake may
	// leave this as a no-op; the default impl auto-sizes a grid from the
	// level's ASeinLevelVolumes so designers get stamping + debug viz before
	// the level has been baked.
	// ----------------------------------------------------------------------

	virtual void InitGridFromVolumes(UWorld* World) {}

	// ----------------------------------------------------------------------
	// Tick — refreshes per-cell visibility from current sources + blockers.
	// Subsystem ticks this at its configured cadence. Impls may compute
	// stamp-delta (fast path) or recompute from scratch (simple path). World
	// is passed in so impls can reach the ECS for source iteration without
	// needing to plumb a GetWorld() override.
	// ----------------------------------------------------------------------

	virtual void TickStamps(UWorld* World) {}

	// ----------------------------------------------------------------------
	// Queries — reader BPFL + LOS delegate route through these.
	// ----------------------------------------------------------------------

	/** Full EVNNNNNN byte at the cell containing `WorldPos`, from `Observer`'s
	 *  VisionGroup. Returns 0 if the position is outside the grid or the impl
	 *  has no runtime data. */
	virtual uint8 GetCellBitfield(FSeinPlayerID Observer, const FFixedVector& WorldPos) const { return 0; }

	/** Convenience: true if any of `LayerMask`'s bits are set in the cell's
	 *  bitfield. Caller passes SEIN_FOW_BIT_NORMAL, SEIN_FOW_MASK_VISIBLE,
	 *  or a custom mask. */
	bool IsCellVisible(FSeinPlayerID Observer, const FFixedVector& WorldPos, uint8 LayerMask = SEIN_FOW_BIT_NORMAL) const
	{
		return (GetCellBitfield(Observer, WorldPos) & LayerMask) != 0;
	}

	/** Convenience: true if the Explored bit is set. Sticky — once true for
	 *  a cell, stays true for the rest of the match. */
	bool IsCellExplored(FSeinPlayerID Observer, const FFixedVector& WorldPos) const
	{
		return (GetCellBitfield(Observer, WorldPos) & SEIN_FOW_BIT_EXPLORED) != 0;
	}

	/** Whether the entity `Target` is currently visible to `Observer`. Single
	 *  source of truth for the fog visibility decision — same check the
	 *  `USeinFogOfWarVisibilitySubsystem` uses to toggle actor visibility,
	 *  and the same check cover queries / minimap / UI should use to gate
	 *  per-player information leakage.
	 *
	 *  Logic, in order:
	 *    1. Observer is invalid → permissive (true). Caller hasn't specified
	 *       a viewer; no filtering.
	 *    2. Target's owner == Observer → visible. Owner always sees their
	 *       own units / buildings / deployments regardless of fog.
	 *    3. `FSeinFogVisibilityComponent::FogVisibilityPolicy`:
	 *       - `AlwaysVisible` → true.
	 *       - `VisibleOnceExplored` → visible iff any cell in the entity's
	 *         footprint has the sticky Explored bit set for `Observer`.
	 *       - `VisionLayersOnly` (default) → visible iff a vision source on
	 *         a matching emission layer is currently stamping the cell.
	 *
	 *  Implementation lives in the cpp so subclasses can override if they
	 *  want different per-player policy. Base impl reads
	 *  `FSeinFogVisibilityComponent` from sim storage for both the emission
	 *  mask AND the persistence policy (single component, two fields —
	 *  authored on `USeinEntityComponent`'s top-level visibility fields and
	 *  auto-injected at spawn by `InjectAuthoredComponents`). */
	virtual bool IsEntityVisibleToObserver(FSeinPlayerID Observer,
		USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const;

	/** Compute the OR of EVNNNNNN bits visible to `Observer` across the
	 *  target entity's volumetric footprint. Replaces the legacy single-
	 *  point check at the entity's transform — a wide tank whose center sits
	 *  one cell off from a watching infantry would otherwise read as
	 *  invisible despite being right there.
	 *
	 *  Subclasses iterate the entity's `FSeinExtentsComponent::Stamps` (if
	 *  present) and OR the bitfields of every covered cell. This base impl
	 *  falls back to a single-point query at the entity transform —
	 *  preserves correctness for entities without an extents component
	 *  (props, projectiles, etc. that don't need volumetric checks). */
	virtual uint8 GetEntityVisibleBits(FSeinPlayerID Observer,
		USeinWorldSubsystem& Sim, FSeinEntityHandle Target) const;

	// ----------------------------------------------------------------------
	// Debug
	// ----------------------------------------------------------------------

	/** Per-frame debug draw hook. Called each tick while the fog show flag is
	 *  on. Default: no-op — the framework's shipped cell viz goes through a
	 *  scene-proxy debug component (lands with the debug pass). */
	virtual void DrawDebug(UWorld* World) const {}

	/** Collect cell geometry for scene-proxy rendering, for one observer's
	 *  VisionGroup. `VisibleBitIndex` in [0, 7] selects which EVNNNNNN bit
	 *  paints as "visible" (0 = E, 1 = V, 2..7 = N0..N5); driven by
	 *  `Sein.FogOfWar.Show.Layer`. Default impls emit
	 *  one quad per cell with per-cell color derived from which bits are
	 *  set (visible / blocker / default). Default virtual: no cells. */
	virtual void CollectDebugCellQuads(FSeinPlayerID Observer,
		int32 VisibleBitIndex,
		TArray<FVector>& OutCenters,
		TArray<FColor>& OutColors,
		float& OutHalfExtent) const {}

	// ----------------------------------------------------------------------
	// Events
	// ----------------------------------------------------------------------

	/** Broadcast after bake completion, substrate adoption, dynamic blocker
	 *  mutation. */
	FSeinOnFogOfWarMutated OnFogOfWarMutated;
};
