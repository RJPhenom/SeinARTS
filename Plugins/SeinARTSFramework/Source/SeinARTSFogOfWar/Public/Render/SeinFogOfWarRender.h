/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarRender.h
 * @brief   Drop-in fog-of-war RENDER actor — a placeable post-process source
 *          that tints the world from the LOCAL observer's vision grid, and
 *          hosts switchable per-layer "visor" views (thermal, IR, …).
 *
 *          Drop one into a level and assign a fog post-process material; it
 *          tints the whole view with no further wiring (its UPostProcessComponent
 *          is unbound, so the actor's position is irrelevant). It does NOT compute
 *          vision — it READS the active USeinFogOfWar. Vision mutations mark
 *          the render data dirty; updates are coalesced at FogRenderTickRate
 *          before baking the local observer's per-cell EVNNNNNN
 *          bitfield into a tiny tint texture (BGRA: rgb = fog color, a = darken
 *          amount), and feeds that + the grid's world bounds to the base fog
 *          material that does `lerp(scene, tint.rgb, tint.a)`.
 *
 *          Base tier mapping (per cell), all tunable below — computed against the
 *          ACTIVE vision layer's bit (see vision layers):
 *            - byte == 0 (never explored)  → UnexploredColor @ UnexploredOpacity (opaque black)
 *            - Explored bit, not visible    → UnexploredColor @ ExploredOpacity   (dimmed memory)
 *            - visible on the active layer  → clear (alpha 0)
 *
 *          Vision layers (the switchable "visor" views): the local player can
 *          switch their view between Normal (the V bit) and any enabled custom
 *          layer (N0..N5) via SetActiveVisionLayer / CycleVisionLayer or the
 *          `Sein.Vision.Layer` console command. Switching does two render-only
 *          things, both client-side and lockstep-safe (the sim already stamps
 *          every layer bit each tick — this just reads a different one):
 *            1. the fog's revealed area follows the active layer's bit (the
 *               texture is rebuilt from that bit; non-additive by default, or
 *               OR'd with Normal per-slot), and
 *            2. that layer's full-screen Post Process Material is composited
 *               UNDER the base fog (so unexplored stays hidden — no map leak).
 *          The active vision layer is per-player view state; it never touches the
 *          sim, so two players can run different layers deterministically.
 *
 *          Sim/render separation: render-layer only. Pure READ of already-computed
 *          (deterministic) vision; never mutates sim state; no fixed-point beyond
 *          float conversions for display. Observer = the local PC's SeinPlayerID
 *          (UE::SeinARTSFogOfWar::ResolveLocalObserverPlayerID).
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
 * One switchable custom vision layer's render treatment. Slot index N maps to
 * EVNNNNNN bit (2 + N) — i.e. VisionLayerPostProcessMaterials[0] = N0, … [5] = N5,
 * matching `USeinARTSCoreSettings::VisionLayers`.
 */
USTRUCT(BlueprintType)
struct FSeinVisionLayerView
{
	GENERATED_BODY()

	/** Switchable. Disabled slots are skipped by CycleVisionLayer and rejected by
	 *  SetActiveVisionLayer — lets you reserve a slot's index without making it a
	 *  live view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War")
	bool bEnabled = false;

	/** Full-screen visor style applied while this layer is the active view (a
	 *  thermal heat palette, IR green, night-vision, …). Composites UNDER the base
	 *  fog, so the fog still hides unexplored area. Leave empty for a layer that
	 *  changes only the revealed fog area without restyling the scene. SOFT ref. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Post Process Material"))
	TSoftObjectPtr<UMaterialInterface> PostProcessMaterial;

	/** Additive reveal. False (default) = a true visor: viewing this layer fogs
	 *  everything except what THIS layer sees. True = the revealed area is this
	 *  layer OR normal vision (you keep your normal sight and gain this layer's). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Combine With Normal Vision"))
	bool bCombineWithNormalVision = false;
};

UCLASS(Blueprintable, meta = (DisplayName = "Sein Fog Of War Render"))
class SEINARTSFOGOFWAR_API ASeinFogOfWarRender : public AActor
{
	GENERATED_BODY()

public:
	ASeinFogOfWarRender();

	// ----------------------------------------------------------------------
	// Tunables — base fog
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
	float ExploredOpacity = 0.75f;

	/** Edge smoothing, in fog cells of blur radius. **0 = NO smoothing** — hard,
	 *  blocky per-cell edges (nearest-filtered, no blur). Above 0 switches on
	 *  bilinear filtering plus a box blur of this radius, spreading the fog edge
	 *  softer and rounding off the per-cell stair-stepping. Cheap (the fog texture
	 *  is tiny). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (ClampMin = "0.0", ClampMax = "5.0", UIMax = "5.0",
			DisplayName = "Smoothing Strength"))
	float SmoothingStrength = 0.1f;

	// ----------------------------------------------------------------------
	// Tunables — switchable vision layers
	// ----------------------------------------------------------------------

	/** Per custom vision layer the player can switch their view to (slot N → layer
	 *  N0..N5, matching Project Settings > SeinARTS > Vision Layers). When the local
	 *  player switches to this layer (SetActiveVisionLayer / CycleVisionLayer, or the
	 *  `Sein.Vision.Layer` console command), this slot's Post Process Material is
	 *  applied FULL-SCREEN on top of the scene but UNDER the base fog — giving that
	 *  layer its own "visor" look (e.g. a thermal heat palette). The base Fog Post
	 *  Process Material keeps handling the unexplored/explored darkening, so this
	 *  material only defines the STYLE, not the fog tint. Leave the material empty
	 *  for a layer that changes the revealed area without restyling; disable a slot
	 *  to make it un-switchable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Fog Of War",
		meta = (EditFixedSize, DisplayName = "Vision Layer Post Process Materials"))
	TArray<FSeinVisionLayerView> VisionLayerPostProcessMaterials;

	// ----------------------------------------------------------------------
	// Runtime API
	// ----------------------------------------------------------------------

	/** The vision layer the local view is currently using: -1 = Normal (the V
	 *  bit), 0..5 = the custom layer in slot N. Client-only view state. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Active Vision Layer"))
	int32 ActiveVisionLayer = -1;

	UFUNCTION(BlueprintPure, Category = "SeinARTS|Fog Of War")
	int32 GetActiveVisionLayer() const { return ActiveVisionLayer; }

	/** Switch the local view to a vision layer: -1 = Normal, 0..5 = custom slot.
	 *  Rebuilds the fog reveal from that layer's bit and swaps in its style
	 *  material. Switching to a disabled / out-of-range custom slot is ignored.
	 *  Pure render/view change — never touches the sim. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Set Active Vision Layer"))
	void SetActiveVisionLayer(int32 LayerIndex);

	/** Cycle the local view through Normal → each ENABLED custom layer → Normal. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Fog Of War",
		meta = (DisplayName = "Cycle Vision Layer"))
	void CycleVisionLayer();

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
	/** Unbound post-process source that carries the style + fog blendables. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FogMID;

	/** The active vision layer's style material, held as a blendable. Null in
	 *  Normal view or when the active slot has no material. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ActiveStyleMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> FogTexture;

	/** Local observer this actor is currently painting for. */
	FSeinPlayerID CachedObserver;
	bool bObserverResolved = false;
	bool bTextureDirty = true;

	int32 TexWidth = 0;
	int32 TexHeight = 0;

	/** BGRA tint, row-major, TexWidth*TexHeight*4 bytes. */
	TArray<uint8> PixelBuffer;

	TWeakObjectPtr<USeinFogOfWar> SubscribedFog;
	FDelegateHandle FogMutatedHandle;

	USeinFogOfWar* ResolveFog() const;

	/** Re-resolve the local observer; returns true if it changed. */
	bool ResolveObserver();

	/** Which EVNNNNNN bits count as "visible" for the active layer: the V bit in
	 *  Normal view, else the active layer's N-bit (optionally OR'd with V when the
	 *  slot opts into Combine With Normal Vision). */
	uint8 ComputeVisibleMask() const;

	/** (Re)create the transient tint texture at WxH if needed; rebinds it to the
	 *  material's FogTexture param. */
	void EnsureTexture(int32 W, int32 H);

	/** Pull the observer's grid, bake tints into PixelBuffer, upload, push world
	 *  bounds to the material. */
	/** Returns true when a valid observer grid was uploaded. */
	bool RebuildTexture();

	/** Stream PixelBuffer to the GPU texture (render-thread safe). */
	void UploadPixels();

	/** Separable box-blur PixelBuffer in place by `Radius` cells (fractional ok),
	 *  to soften/spread the fog edge beyond the bilinear filter. */
	void BlurPixelBuffer(float Radius);

	/** Map one EVNNNNNN byte → tint color + darken alpha for the given visible mask. */
	FLinearColor TintForCell(uint8 Bits, uint8 VisibleMask) const;

	/** Load/clear the active layer's style material to match ActiveVisionLayer. */
	void UpdateStyleBlendable();

	/** Rebuild the post-process blendable list in order: style UNDER, fog ON TOP. */
	void RefreshBlendables();

	void HandleFogMutated();

	static const FName P_FogTexture;
	static const FName P_WorldMin;
	static const FName P_WorldSize;
};
