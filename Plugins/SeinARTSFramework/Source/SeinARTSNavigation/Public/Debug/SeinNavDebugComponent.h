/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavDebugComponent.h
 * @brief   Scene-proxy-backed debug viz for the active USeinNavigation.
 *
 *          Hosted on ASeinLevelVolume: the module registers this class with the
 *          volume's debug-component registry at startup, and the volume attaches
 *          one transient instance in PostRegisterAllComponents. At proxy
 *          creation time it calls `USeinNavigation::CollectDebugCellQuads` to
 *          snapshot cell geometry, then emits a single batched mesh per view
 *          via `FDynamicMeshBuilder`. The proxy's `GetViewRelevance` consults
 *          `FSceneView::EngineShowFlags.Navigation` so UE's 'P' key + the
 *          `Sein.Nav.Show` console command drive visibility
 *          without any per-frame cost when off.
 *
 *          Subscribes to `USeinNavigation::OnNavigationMutated` — bake
 *          completion / asset swap / dynamic obstacle change triggers
 *          `MarkRenderStateDirty`, which causes UE to rebuild the proxy with
 *          fresh cell data.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "SeinNavDebugComponent.generated.h"

class USeinNavigation;
#if UE_ENABLE_DEBUG_DRAWING
struct FSeinNavDebugStaticSnapshot;
#endif

UCLASS(ClassGroup = (SeinARTS), meta = (DisplayName = "Sein Nav Debug Component"))
class SEINARTSNAVIGATION_API USeinNavDebugComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	USeinNavDebugComponent();

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:

	/** Called whenever the active nav broadcasts OnNavigationMutated. */
	void HandleNavMutated();

	/** Editor-idle preview. In editor (pre-PIE) the subsystems don't auto-load
	 *  baked level data, so when no live nav has runtime data this reads the
	 *  owning ASeinLevelVolume's BakedAsset directly and emits per-cell quads +
	 *  colors from its baked "Nav" channel (0 / 255 cost = blocked red, else green
	 *  or the cell's terrain-type DebugColor). Parallel arrays, bucketed by color. */
	void CollectAssetPreviewQuads(TArray<FVector>& OutCenters, TArray<FColor>& OutColors, float& OutHalfExtent) const;

	/** Weak pointer so the scene proxy can unsubscribe safely when the nav
	 *  is swapped out or the component is destroyed. */
	TWeakObjectPtr<USeinNavigation> SubscribedNav;

	FDelegateHandle NavMutatedHandle;

#if UE_ENABLE_DEBUG_DRAWING
	/** Immutable static-cell snapshot shared with each render-thread proxy.
	 *  Dynamic blockers may change every sim tick; retaining this snapshot means
	 *  those changes rebuild only the small blocker overlay, not the full baked
	 *  nav grid. The nav's monotonic static generation is the invalidation key. */
	TSharedPtr<const FSeinNavDebugStaticSnapshot, ESPMode::ThreadSafe> CachedStaticSnapshot;
	TWeakObjectPtr<USeinNavigation> CachedStaticNav;
	uint64 CachedStaticGeneration = MAX_uint64;
#endif
};
