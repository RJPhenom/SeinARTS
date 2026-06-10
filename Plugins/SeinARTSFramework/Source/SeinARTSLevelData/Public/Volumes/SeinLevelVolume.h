/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelVolume.h
 * @brief   Unified level-data bounds volume (CP1.1, planning/Decisions.md D10).
 *
 *          Replaces ASeinNavVolume + ASeinFogOfWarVolume: drop ONE into a level,
 *          shape its brush (the play area), click "Bake Level Data". Multiple volumes
 *          union (play area = union of brush shapes; cells outside every brush are
 *          out-of-bounds). Holds the polymorphic baked asset + per-layer config
 *          sections. The active USeinLevelData subclass (plugin settings) owns bake
 *          semantics; this actor just defines bounds + config.
 *
 *          Mirrors ASeinNavVolume (NoCollision static brush, PostEditMove bounds
 *          snapshot for cross-platform determinism). The Fog Of War config section
 *          is added at the FoW-port step; for now it carries the shared/Navigation
 *          config the nav layer reads.
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
	// Navigation layer config (mirrors ASeinNavVolume; also the shared grid
	// resolution until per-layer resolution overrides land).
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
	// Output
	// ----------------------------------------------------------------------

	/** Baked level data for this level. Assigned by the bake pipeline; shared across
	 *  all level volumes on the level (last-baked wins). Polymorphic — concrete type
	 *  depends on the active USeinLevelData subclass. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Output")
	TObjectPtr<USeinLevelDataAsset> BakedAsset;

	// ----------------------------------------------------------------------
	// Editor-baked AABB snapshot — cross-platform-determinism (mirrors ASeinNavVolume).
	// PostEditMove writes (editor-process FromFloat → serialized to .umap); runtime
	// grid init reads. `bBoundsBaked` distinguishes fresh actors from legacy data.
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

	/** Editor button: bake the unified level data covering every Sein Level Volume
	 *  in this level (runs the shared trace pass + all registered layer providers).
	 *  Routes through USeinLevelDataSubsystem::BeginBake. */
	UFUNCTION(CallInEditor, Category = "SeinARTS|Build", meta = (DisplayName = "Bake Level Data"))
	void BakeLevelData();
};
