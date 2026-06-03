/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarVolume.h
 * @brief   Fog-of-war bake-bounds volume. Drop into a level, scale it, click
 *          "Bake Fog Of War" on the details panel. Multiple volumes union
 *          into one baked asset (last-baked wins).
 *
 *          Fully decoupled from navigation. A project that runs custom nav
 *          + stock fog lands two volumes on the level: ASeinNavVolume and
 *          ASeinFogOfWarVolume, at whatever independent cell sizes the two
 *          subsystems want. Neither reads the other.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinFogOfWarVolume.generated.h"

class USeinFogOfWarAsset;
class USeinFogOfWarDebugComponent;

UCLASS(meta = (DisplayName = "Sein Fog Of War Volume"))
class SEINARTSFOGOFWAR_API ASeinFogOfWarVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinFogOfWarVolume(const FObjectInitializer& ObjectInitializer);

	/** Override the project-wide default fog cell size. When false, the plugin
	 *  settings' `VisionCellSize` is used. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Overrides")
	bool bOverrideCellSize = false;

	UPROPERTY(EditAnywhere, Category = "SeinARTS|Overrides",
		meta = (EditCondition = "bOverrideCellSize"))
	FFixedPoint CellSize = FFixedPoint::FromInt(400);

	/** Baked fog data for this level. Assigned by the bake pipeline; shared
	 *  across all ASeinFogOfWarVolumes on the level (last-baked wins).
	 *  Polymorphic — concrete type depends on the active USeinFogOfWar
	 *  subclass. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Output")
	TObjectPtr<USeinFogOfWarAsset> BakedAsset;

	/** If true, the bake's per-cell pass detects static sight blockers
	 *  (walls, buildings, hedgerows) and stamps them into the asset. If
	 *  false, the bake only captures grid layout + ground height —
	 *  BlockerHeight stays zero everywhere and all sight occlusion comes
	 *  from runtime sources (`USeinExtentsComponent` with bBlocksFogOfWar, designer-
	 *  authored ability effects, etc.). Set before clicking Bake Fog Of
	 *  War. When multiple volumes exist on a level, the first one's
	 *  setting wins — same convention as `CellSize`. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Build",
		meta = (DisplayName = "Bake Static Blockers"))
	bool bBakeStaticBlockers = true;

	/** Scene-proxy-backed cell viz. Driven by `ShowFlags.FogOfWar` /
	 *  `Sein.FogOfWar.Show`; null in shipping. */
	UPROPERTY(VisibleAnywhere, Transient, Category = "SeinARTS|Debug")
	TObjectPtr<USeinFogOfWarDebugComponent> DebugComponent;

	/** Editor-baked snapshot of the volume's world AABB, computed from
	 *  brush geometry whenever the volume moves or is resized in the
	 *  editor (`PostEditMove`). The fog grid init reads these directly
	 *  rather than the float `FBox` from the brush — conversion happens
	 *  once in the editor process and the int64 bits are serialized to
	 *  the .umap, so cross-arch clients never run FromFloat at level
	 *  load. `bBoundsBaked` flips true on first snapshot. */
	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedBoundsMin = FFixedVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	FFixedVector PlacedBoundsMax = FFixedVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, AdvancedDisplay, Category = "SeinARTS|Determinism")
	bool bBoundsBaked = false;

#if WITH_EDITOR
	/** Snapshot bounds at edit time so runtime never converts. */
	virtual void PostEditMove(bool bFinished) override;
#endif

	/** World-space AABB of this volume's brush (float, render-side). Use
	 *  `PlacedBounds*` for sim-side reads instead. */
	FBox GetVolumeWorldBounds() const;

	/** Per-volume cell size with plugin-settings fallback. Returns
	 *  FFixedPoint so consumers stay deterministic — no FromFloat needed
	 *  on the runtime path. */
	FFixedPoint GetResolvedCellSize() const;
};
