/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDebugComponent.h
 * @brief   Scene-proxy-backed debug viz for the active USeinFogOfWar.
 *
 *          Hosted on ASeinLevelVolume — the FoW module registers this class
 *          via `ASeinLevelVolume::RegisterDebugComponentClass` at module
 *          startup and the volume attaches a transient instance. Proxy emits
 *          one batched mesh of cell quads gated by the custom
 *          `ShowFlags.FogOfWar` (registered by the module; toggled by
 *          `Sein.FogOfWar.Show`). Non-PIE path parses the owning volume's
 *          baked "FogOfWar" level-data channel (real grid + blockers); when
 *          no bake exists it rasterizes the volume's bounds into cells at the
 *          volume's resolved vision cell size so designers get immediate viz.
 *
 *          Subscribes to `USeinFogOfWar::OnFogOfWarMutated` — bake /
 *          substrate adoption / dynamic blocker change triggers
 *          `MarkRenderStateDirty`, forcing UE to rebuild the proxy with
 *          fresh cell data. Mirrors `USeinNavDebugComponent`'s behavior.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "SeinFogOfWarDebugComponent.generated.h"

class USeinFogOfWar;

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Sein Fog Of War Debug Component"))
class SEINARTSFOGOFWAR_API USeinFogOfWarDebugComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	USeinFogOfWarDebugComponent();

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:

	/** Called whenever the active fog impl broadcasts OnFogOfWarMutated. */
	void HandleFogMutated();

	/** In editor (pre-PIE) the world subsystem doesn't auto-load fog volume
	 *  assets — LoadBakedAssetIntoFogOfWar only runs on OnWorldBeginPlay, which
	 *  never fires for an editor world. This hook grabs the owning volume's
	 *  baked asset and pushes it into the active fog impl so the scene proxy
	 *  draws the real baked grid instead of the all-red bounds fallback.
	 *  Mirrors USeinNavDebugComponent::EnsureNavLoaded. */
	void EnsureFogLoaded();

	TWeakObjectPtr<USeinFogOfWar> SubscribedFog;
	FDelegateHandle FogMutatedHandle;

	// Cached no-bake fallback rasterization. The fallback CreateSceneProxy path
	// traces one ray per (strided) cell down onto world static geometry — up to
	// ~20k synchronous complex line traces — to position the debug cells. That
	// output is a pure function of the volume's world bounds + resolved cell
	// size, so it is cached and only recomputed when those inputs change, rather
	// than on every proxy rebuild (re-register / undo / fog-mutation). Persisted
	// across re-registers so undo, unrelated edits, and recompiles that
	// re-register the component don't re-pay the trace. The cache auto-
	// invalidates when bounds / cell size change (the compare in CreateSceneProxy).
	// Editor-debug-viz only; plain transient members, never serialized.
	TArray<FVector> CachedFallbackCenters;
	float CachedFallbackHalfExtent = 0.0f;
	FBox CachedFallbackBounds = FBox(ForceInit);
	float CachedFallbackCellSize = 0.0f;
	bool bHasFallbackCache = false;
};
