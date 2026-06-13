/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelVolume.h
 * @brief   Unified level-data bounds volume (CP1.1, planning/Decisions.md D10).
 *
 *          Replaces the legacy ASeinNavVolume + ASeinFogOfWarVolume (removed):
 *          drop ONE into a level, shape its brush (the play area), click "Bake
 *          Level Data". Multiple volumes union (play area = union of brush shapes;
 *          cells outside every brush are out-of-bounds). Holds the polymorphic
 *          baked asset + per-layer config sections (Navigation, Fog Of War). The
 *          active USeinLevelData subclass (plugin settings) owns bake semantics;
 *          this actor just defines bounds + config. NoCollision static brush;
 *          PostEditMove bounds snapshot for cross-platform determinism. Layer
 *          modules attach their debug-viz components via the
 *          RegisterDebugComponentClass registry below.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinLevelVolume.generated.h"

class USeinLevelDataAsset;

UCLASS(meta = (DisplayName = "Sein Level Volume"))
class SEINARTSLEVELDATA_API ASeinLevelVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinLevelVolume(const FObjectInitializer& ObjectInitializer);

	// ----------------------------------------------------------------------
	// Navigation layer config (also the shared grid resolution — the substrate
	// bakes at the finest layer resolution, which is nav's).
	// ----------------------------------------------------------------------

	/** Override the project-wide cell size. When false, plugin settings' `CellSize`. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation")
	bool bOverrideCellSize = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation",
		meta = (EditCondition = "bOverrideCellSize"))
	FFixedPoint CellSize = FFixedPoint::FromInt(100);

	/** Maximum vertical step (world units) an agent can traverse between adjacent
	 *  cells. Blocks "jumps" across gaps where two cells share a surface Z but have
	 *  no bridging geometry. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation")
	bool bOverrideMaxStepHeight = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Navigation",
		meta = (EditCondition = "bOverrideMaxStepHeight"))
	FFixedPoint MaxStepHeight = FFixedPoint::FromInt(50);

	// ----------------------------------------------------------------------
	// Fog Of War layer config (read by the fog layer provider at bake —
	// first volume wins, like cell size).
	// ----------------------------------------------------------------------

	/** Override the project-wide fog cell size. When false, plugin settings'
	 *  `VisionCellSize`. Snapped to an integer multiple of the shared grid's
	 *  cell size at bake (the channel-resolution contract). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War")
	bool bOverrideVisionCellSize = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War",
		meta = (EditCondition = "bOverrideVisionCellSize"))
	FFixedPoint VisionCellSize = FFixedPoint::FromInt(400);

	/** If true, the fog layer's bake detects static sight blockers (walls,
	 *  buildings, hedgerows) and stamps them into the fog channel. If false,
	 *  only grid layout + ground height bake — all sight occlusion then comes
	 *  from runtime sources (`USeinExtentsComponent` with bBlocksFogOfWar,
	 *  designer-authored ability effects, etc.). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Bake Static Blockers"))
	bool bBakeStaticBlockers = true;

	// ----------------------------------------------------------------------
	// Bake — the bake action + the asset it produces. Presented as a "Bake"
	// sub-group under the shared "SeinARTS" details category (alongside
	// Navigation / Fog Of War), with the button just above BakedAsset. That
	// nesting is built by FSeinLevelVolumeDetails (registered by this module in
	// the editor): a CallInEditor button can't ride the "A|B" sub-category
	// nesting — its category is forced through EditCategory, which pulls it out
	// into a detached top-level "SeinARTS|Bake" header — so BakeLevelData is a
	// plain method here and the customization draws the button. BakedAsset is
	// authored at plain "SeinARTS" (guaranteeing a real parent category for the
	// customization to edit); the customization hides that default row and
	// re-adds it inside the "Bake" group, beneath the button.
	// ----------------------------------------------------------------------

	/** Bake the unified level data covering every Sein Level Volume in this level
	 *  (runs the shared trace pass + all registered layer providers). Routes through
	 *  USeinLevelDataSubsystem::BeginBake. Surfaced as the "Bake Level Data" button
	 *  in the details panel's "Bake" group (see FSeinLevelVolumeDetails). */
	void BakeLevelData();

	/** Baked level data for this level. Assigned by the bake pipeline; shared across
	 *  all level volumes on the level (last-baked wins). Polymorphic — concrete type
	 *  depends on the active USeinLevelData subclass. SOFT reference — loaded on demand
	 *  (at begin-play by the level-data subsystem), so opening the map does NOT
	 *  force-load the baked substrate package into the level's hard-reference graph. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS")
	TSoftObjectPtr<USeinLevelDataAsset> BakedAsset;

	// ----------------------------------------------------------------------
	// Editor-baked AABB snapshot — cross-platform determinism. PostEditMove
	// writes (editor-process FromFloat → serialized to .umap); runtime grid
	// init reads. `bBoundsBaked` distinguishes fresh actors from legacy data.
	// ----------------------------------------------------------------------
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedBoundsMin = FFixedVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedBoundsMax = FFixedVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	bool bBoundsBaked = false;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** World-space AABB of this volume's brush (float, render-side). Use `PlacedBounds*`
	 *  for sim-side reads. */
	FBox GetVolumeWorldBounds() const;

	/** Per-volume cell size with plugin-settings fallback. */
	FFixedPoint GetResolvedCellSize() const;

	/** Per-volume max-step-height with plugin-settings fallback. */
	FFixedPoint GetResolvedMaxStepHeight() const;

	/** Per-volume fog cell size with plugin-settings fallback (`VisionCellSize`). */
	FFixedPoint GetResolvedVisionCellSize() const;

	// ----------------------------------------------------------------------
	// Debug-viz component registry. The nav / FoW debug scene-proxy components
	// (cell viz) are hosted on this volume, but the LevelData module cannot
	// link against the layer modules (dependencies point the other way) — so
	// each layer module REGISTERS its component class at module startup (the
	// framework's draw-callback-registry idiom) and the volume attaches one
	// transient instance of each registered class in PostRegisterAllComponents.
	// Non-shipping only (mirrors the legacy volumes' debug hosting).
	// ----------------------------------------------------------------------

	/** Register/unregister a debug component class to auto-attach to every
	 *  ASeinLevelVolume. Call from the owning module's Startup/ShutdownModule. */
	static void RegisterDebugComponentClass(UClass* ComponentClass);
	static void UnregisterDebugComponentClass(UClass* ComponentClass);

	virtual void PostRegisterAllComponents() override;
};
