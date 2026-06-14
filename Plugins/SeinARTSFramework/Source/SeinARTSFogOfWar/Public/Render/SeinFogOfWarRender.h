/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarRender.h
 * @brief   Drop-in fog-of-war RENDER actor — a placeable post-process source
 *          that tints the world from the LOCAL observer's vision grid.
 *
 *          Drop one into a level and assign a fog post-process material; it
 *          tints the whole view with no further wiring (its UPostProcessComponent
 *          is unbound, so the actor's position is irrelevant). It does NOT compute
 *          vision — it READS the active USeinFogOfWar each time vision changes
 *          (OnFogOfWarMutated), bakes the local observer's per-cell EVNNNNNN
 *          bitfield into a tiny tint texture (BGRA: rgb = fog color, a = darken
 *          amount), and feeds that + the grid's world bounds to a post-process
 *          material that does `lerp(scene, tint.rgb, tint.a)`.
 *
 *          Tier mapping (per cell), all tunable below:
 *            - byte == 0 (never explored)  → UnexploredColor @ UnexploredOpacity (opaque black)
 *            - Explored bit, no Normal bit → UnexploredColor @ ExploredOpacity   (dimmed memory)
 *            - Normal (V) bit              → clear (alpha 0)
 *            - custom N0..N5 bits          → per-layer tint blended over the base
 *
 *          Sim/render separation: this is render-layer only. It performs a pure
 *          READ of already-computed (deterministic) vision; it never mutates sim
 *          state and touches no fixed-point math beyond float conversions for
 *          display. Observer = the local PC's SeinPlayerID
 *          (UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID).
 *
 *          The post-process material is a designer-authored content asset (see
 *          the FogPostProcessMaterial recipe in the plugin docs). This actor
 *          owns the data → texture path; the material owns the look composite.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/SeinPlayerID.h"
#include "SeinFogOfWarRender.generated.h"

class USeinFogOfWar;
class UPostProcessComponent;
class UTexture2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * One designer-configurable custom layer's render treatment. Slot index N maps
 * to EVNNNNNN bit (2 + N) — i.e. CustomLayers[0] = N0, ..., CustomLayers[5] = N5,
 * matching `USeinARTSCoreSettings::VisionLayers`. When a cell has this layer's
 * bit set and the slot is enabled, the cell tint is blended toward Color by
 * Opacity (over whatever the base E/V tier produced), and the darken alpha is
 * raised to at least Opacity so the layer shows even over currently-visible
 * terrain (e.g. a faint radar tint).
 */
USTRUCT(BlueprintType)
struct FSeinFogLayerRenderConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War")
	bool bEnabled = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Opacity = 0.5f;
};

UCLASS(Blueprintable, meta = (DisplayName = "Sein Fog Of War Render"))
class SEINARTSFOGOFWAR_API ASeinFogOfWarRender : public AActor
{
	GENERATED_BODY()

public:
	ASeinFogOfWarRender();

	// ----------------------------------------------------------------------
	// Tunables
	// ----------------------------------------------------------------------

	/** Post-process material that composites the fog tint over the scene. Reads
	 *  texture param `FogTexture` (the per-cell tint, BGRA) and vector params
	 *  `FogWorldMin` / `FogWorldSize` (world-XY rect the texture spans). Authored
	 *  in the material editor — see the plugin docs for the node recipe. Without
	 *  it, the overlay is inert (logged). SOFT ref so placing the actor doesn't
	 *  force-load the material into every map that references it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War")
	TSoftObjectPtr<UMaterialInterface> FogPostProcessMaterial;

	/** Tint applied to fogged (unexplored + explored-not-visible) cells. Black
	 *  is the classic RTS look; raise toward a blue/grey for stylized fog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War")
	FLinearColor UnexploredColor = FLinearColor::Black;

	/** Opacity over NEVER-explored cells. 1.0 = opaque (can't see the map at all). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Unexplored Opacity"))
	float UnexploredOpacity = 1.0f;

	/** Opacity over EXPLORED-but-not-currently-visible cells — the dimmed
	 *  "terrain memory" tier. The headline tunable: 0 = no dimming, 1 = as dark
	 *  as unexplored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Explored Opacity"))
	float ExploredOpacity = 0.5f;

	/** Bilinear-filter the tint texture for soft fog edges (vs hard per-cell
	 *  blocks). On by default — the fog grid is coarse, so soft edges read far
	 *  better. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Smooth Edges"))
	bool bSmoothEdges = true;

	/** Per-custom-layer render treatment. Fixed 6 slots: index N → EVNNNNNN bit
	 *  (2 + N) → the layer named in `USeinARTSCoreSettings::VisionLayers[N]`. All
	 *  disabled by default; the three core tiers work with none enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (EditFixedSize, DisplayName = "Custom Layers (N0..N5)"))
	TArray<FSeinFogLayerRenderConfig> CustomLayers;

	/** Force a full rebuild of the tint texture (e.g. after switching the
	 *  displayed observer in an observer/replay camera). Normally unnecessary —
	 *  the actor rebuilds automatically when vision changes. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Refresh Fog Render"))
	void RefreshFogRender();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif

private:
	/** Unbound post-process source that carries the fog material blendable. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FogMID;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FogTexture;

	/** Local observer this actor is currently painting for. */
	FSeinPlayerID CachedObserver;
	bool bObserverResolved = false;

	int32 TexWidth = 0;
	int32 TexHeight = 0;

	/** BGRA tint, row-major, TexWidth*TexHeight*4 bytes. */
	TArray<uint8> PixelBuffer;

	TWeakObjectPtr<USeinFogOfWar> SubscribedFog;
	FDelegateHandle FogMutatedHandle;

	USeinFogOfWar* ResolveFog() const;

	/** Re-resolve the local observer; returns true if it changed. */
	bool ResolveObserver();

	/** (Re)create the transient tint texture at WxH if needed; rebinds it to the
	 *  material's FogTexture param. */
	void EnsureTexture(int32 W, int32 H);

	/** Pull the observer's grid, bake tints into PixelBuffer, upload, push world
	 *  bounds to the material. */
	void RebuildTexture();

	/** Stream PixelBuffer to the GPU texture (render-thread safe). */
	void UploadPixels();

	/** Map one EVNNNNNN byte → tint color + darken alpha using the tunables. */
	FLinearColor TintForCell(uint8 Bits) const;

	void HandleFogMutated();

	static const FName P_FogTexture;
	static const FName P_WorldMin;
	static const FName P_WorldSize;
};
