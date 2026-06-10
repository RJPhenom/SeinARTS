/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavVolume.h
 * @brief   Nav-bake bounds volume. Parallels UE's ANavMeshBoundsVolume — drop
 *          it into a level, scale it, click "Rebuild Sein Nav" on the details
 *          panel. Multiple volumes union into one baked asset.
 *
 *          Decoupling: this actor knows nothing about how nav is baked or
 *          queried. It just defines bounds + holds a polymorphic baked-asset
 *          reference. The active USeinNavigation subclass (see plugin settings)
 *          owns bake semantics.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinNavVolume.generated.h"

class USeinNavigationAsset;
class USeinNavDebugComponent;

UCLASS(meta = (DisplayName = "Sein Nav Volume"))
class SEINARTSNAVIGATION_API ASeinNavVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinNavVolume(const FObjectInitializer& ObjectInitializer);

	/** Override the project-wide cell size. When false, the plugin
	 *  settings' `CellSize` is used. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Overrides")
	bool bOverrideCellSize = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Overrides",
		meta = (EditCondition = "bOverrideCellSize"))
	FFixedPoint CellSize = FFixedPoint::FromInt(100);

	/** Maximum vertical step (world units) an agent can traverse between
	 *  adjacent cells. Blocks "jumps" across gaps where two cells happen to
	 *  have matching surface Zs but no physical geometry bridging them:
	 *  ramp-top → cube-top across thin air is rejected because the midpoint
	 *  surface (ground below) drops more than this value. Typical value is a
	 *  fraction of a cell (~30-60 cm for a 100 cm grid). */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Overrides")
	bool bOverrideMaxStepHeight = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Overrides",
		meta = (EditCondition = "bOverrideMaxStepHeight"))
	FFixedPoint MaxStepHeight = FFixedPoint::FromInt(50);

	/** Baked nav data for this level. Assigned by the bake pipeline; shared
	 *  across all NavVolumes on the level (last-baked wins). Polymorphic —
	 *  concrete type depends on the active USeinNavigation subclass.
	 *  SOFT reference — loaded on demand (at begin-play by the nav subsystem, or
	 *  when the editor viz is shown), so opening the map does NOT force-load the
	 *  baked grid package into memory as part of the level's hard-reference graph. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Output")
	TSoftObjectPtr<USeinNavigationAsset> BakedAsset;

	/** Scene-proxy-backed cell viz. Driven by `ShowFlags.Navigation` /
	 *  `Sein.Nav.Show`; null in shipping. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "SeinARTS|Debug")
	TObjectPtr<USeinNavDebugComponent> DebugComponent;

	/** Editor-baked AABB snapshot — see ASeinFogOfWarVolume header for the
	 *  cross-platform-determinism rationale. PostEditMove writes; nav grid
	 *  init reads. Migration: `bBoundsBaked` distinguishes fresh actors
	 *  from legacy data. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedBoundsMin = FFixedVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedBoundsMax = FFixedVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	bool bBoundsBaked = false;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** World-space AABB of this volume's brush (float, render-side). Use
	 *  `PlacedBounds*` for sim-side reads. */
	FBox GetVolumeWorldBounds() const;

	/** Per-volume cell size with plugin-settings fallback. Returns
	 *  FFixedPoint so consumers stay deterministic — no FromFloat needed
	 *  on the runtime path. */
	FFixedPoint GetResolvedCellSize() const;

	/** Per-volume max-step-height with plugin-settings fallback
	 *  (`USeinARTSCoreSettings::MaxStepHeight`). Returns
	 *  FFixedPoint for the same reason as `GetResolvedCellSize`. */
	FFixedPoint GetResolvedMaxStepHeight() const;
};
