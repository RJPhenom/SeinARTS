/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelData.h
 * @brief   Abstract, swappable substrate for the unified level-bake pipeline (Phase 1, CP1.1).
 *
 *          USeinLevelData owns the CANONICAL grid for one world — bounds / origin /
 *          coordinate space + the shared per-cell height field (traced ONCE) — plus
 *          the bake orchestration and a registry of layer providers. Navigation and
 *          Fog of War are LAYER PROVIDERS that register here, contribute their channels
 *          at bake time, and read the shared substrate at runtime; their consumer-facing
 *          surfaces (USeinNavigation / USeinFogOfWar) are UNCHANGED.
 *
 *          Modularity (planning/Decisions.md D12): the whole substrate is swappable via
 *          `USeinARTSCoreSettings::LevelDataClass`. A team can (Axis A) register a custom
 *          layer provider against the default substrate, or (Axis B) replace the substrate
 *          entirely with one exposing this same surface + provider registry. Consumers never
 *          see this class — they talk to the nav / FoW / LoS surfaces. Modularity != isolation:
 *          the shared substrate is the cooperation seam, behind a swappable interface.
 *
 *          The default impl is the unified single-trace grid (USeinLevelDataDefault, added
 *          next). Bake is the float->fixed boundary (editor traces in float, quantized to
 *          fixed; deterministic at runtime).
 *
 *          THREADING CONTRACT (planning/Roadmap_Multithreading.md, step 1): the READ queries
 *          (GetSharedHeightAt / IsInBounds / GetDimensions / GetOrigin / GetFinestCellSize)
 *          MAY be called concurrently — impls must be reentrant (per-thread scratch if any
 *          mutable buffer is used). BeginBake / LoadFromAsset / Register|UnregisterLayerProvider
 *          are single-threaded. We are single-threaded today; this only reserves the contract.
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinLevelData.generated.h"

class UWorld;
class USeinLevelDataAsset;
class ISeinLevelLayerProvider;
class UTexture2D;

/** Per-cell shared surface data a layer provider reads to reproduce its channel —
 *  the output of the one shared trace pass (D13). Queried by finest-res cell index. */
struct FSeinLevelCellSurface
{
	FFixedPoint Height = FFixedPoint::Zero;   // top-of-surface Z (trace floor if no surface)
	FFixedPoint NormalZ = FFixedPoint::Zero;  // surface normal · Up (for slope gates)
	bool bHasSurface = false;                 // the down-trace found geometry
	bool bInBounds = false;                   // cell center inside a volume brush (D10)
};

/** Fired after the substrate's baked data mutates (bake finished, asset swapped).
 *  Cached consumers (nav/FoW runtime grids, debug proxies) re-query on this signal. */
DECLARE_MULTICAST_DELEGATE(FSeinOnLevelDataMutated);

UCLASS(Abstract, BlueprintType, meta = (DisplayName = "Sein Level Data"))
class SEINARTSLEVELDATA_API USeinLevelData : public UObject
{
	GENERATED_BODY()

public:

	// ----------------------------------------------------------------------
	// Lifecycle — called by the owning subsystem
	// ----------------------------------------------------------------------
	virtual void OnInitialized(UWorld* World) {}
	virtual void OnDeinitialized() {}

	// ----------------------------------------------------------------------
	// Canonical grid / coordinate space — at the FINEST layer resolution (D13).
	// Coarser layers (e.g. FoW) operate their own grid at their own resolution by
	// subsampling this one; they share only the ORIGIN + coordinate space.
	// ----------------------------------------------------------------------

	/** World units per finest grid cell edge. */
	virtual FFixedPoint GetFinestCellSize() const { return FFixedPoint::FromInt(100); }

	/** World-space XY (and Z) of cell (0,0)'s corner. */
	virtual FFixedVector GetOrigin() const { return FFixedVector::ZeroVector; }

	/** Grid dimensions in finest cells (X, Y). */
	virtual FIntPoint GetDimensions() const { return FIntPoint::ZeroValue; }

	/** True if a world-space XY is inside the play-area mask — the union of all
	 *  level-volume brush shapes (D10). Cells outside every brush are out-of-bounds
	 *  (impassable for nav, no vision for FoW). Default: false (no bake). */
	virtual bool IsInBounds(const FFixedVector& WorldPos) const { return false; }

	// ----------------------------------------------------------------------
	// Shared height field — traced ONCE for every layer (the dedup win; D13).
	// ----------------------------------------------------------------------

	/** Sample the baked top-of-surface Z at a world-space XY. This is the SHARED
	 *  source every layer reads (nav for Z-snap, FoW for LOS occlusion). Any
	 *  layer-specific gating (e.g. nav's walkable-only refusal) lives on that
	 *  layer, NOT here. Returns false out-of-bounds / when there is no bake. */
	virtual bool GetSharedHeightAt(const FFixedVector& WorldPos, FFixedPoint& OutZ) const { return false; }

	/** Per-cell shared surface data by finest-res cell index (row-major). The
	 *  provider-facing read: a layer's BakeLayer iterates cells and reads this to
	 *  reproduce its channel (e.g. nav: Cost from slope gate on NormalZ + flags).
	 *  Exposed on the base so providers stay decoupled from the concrete substrate
	 *  (modularity D12). Returns false for an invalid index / no bake. */
	virtual bool GetCellSurface(int32 CellIndex, FSeinLevelCellSurface& OutSurface) const { return false; }

	/** Read a layer's baked channel blob by id (e.g. "Nav"), produced by that
	 *  layer's provider at bake time. The layer's runtime consumer deserializes it
	 *  (nav: Cost + Connections). Returns false if no such channel / no bake. */
	virtual bool GetLayerChannel(FName LayerId, TArray<uint8>& OutData) const { return false; }

	/** The baked top-down minimap background texture for this level (synthesized from
	 *  the shared height field at bake time). Null if the substrate has no bake / no
	 *  minimap output. The UI layer prefers a per-level designer override
	 *  (ASeinLevelVolume::MinimapOverrideTexture) over this. Default: null. */
	virtual UTexture2D* GetMinimapTexture() const { return nullptr; }

	// ----------------------------------------------------------------------
	// Layer-provider registry (D12) — idiomatic to the framework's existing
	// cross-module resolver / draw-callback-registry pattern. A provider
	// (nav, FoW, later terrain-cost) registers, then contributes its channels
	// during BeginBake and reads the shared substrate at runtime.
	// ----------------------------------------------------------------------
	virtual void RegisterLayerProvider(ISeinLevelLayerProvider* Provider) {}
	virtual void UnregisterLayerProvider(ISeinLevelLayerProvider* Provider) {}

	// ----------------------------------------------------------------------
	// Bake (editor / dev-loop) — ONE pass that runs every registered provider
	// (one "Bake Level Data" button). Synchronous, like the nav/FoW bakes today.
	// ----------------------------------------------------------------------
	virtual bool BeginBake(UWorld* World) { return false; }
	virtual bool IsBaking() const { return false; }
	virtual void RequestCancelBake() {}

	// ----------------------------------------------------------------------
	// Runtime load
	// ----------------------------------------------------------------------
	virtual void LoadFromAsset(USeinLevelDataAsset* Asset) {}
	virtual bool HasRuntimeData() const { return false; }

	/** Broadcast after bake completion / asset swap / mutation. */
	FSeinOnLevelDataMutated OnLevelDataMutated;
};
