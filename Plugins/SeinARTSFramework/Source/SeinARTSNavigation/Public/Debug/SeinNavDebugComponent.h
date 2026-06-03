/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavDebugComponent.h
 * @brief   Scene-proxy-backed debug viz for the active USeinNavigation.
 *
 *          Attached to ASeinNavVolume in the actor's constructor. At proxy
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

	/** In editor (pre-PIE) the world subsystem doesn't auto-load NavVolume
	 *  assets. This hook grabs the owning volume's baked asset and pushes it
	 *  into the active nav so the scene proxy has something to draw. */
	void EnsureNavLoaded();

	/** Weak pointer so the scene proxy can unsubscribe safely when the nav
	 *  is swapped out or the component is destroyed. */
	TWeakObjectPtr<USeinNavigation> SubscribedNav;

	FDelegateHandle NavMutatedHandle;
};
