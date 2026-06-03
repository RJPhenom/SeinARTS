/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDebugComponent.h
 * @brief   Scene-proxy-backed debug viz for the active USeinFogOfWar.
 *
 *          Attached to ASeinFogOfWarVolume in the actor's constructor.
 *          Proxy emits one batched mesh of red cell quads gated by the
 *          custom `ShowFlags.FogOfWar` (registered by the module; toggled
 *          by `Sein.FogOfWar.Show`). Non-PIE path rasterizes the
 *          owner volume's bounds into cells at the volume's resolved cell
 *          size so designers get immediate viz without needing a bake.
 *
 *          Subscribes to `USeinFogOfWar::OnFogOfWarMutated` — bake / asset
 *          swap / dynamic blocker change triggers `MarkRenderStateDirty`,
 *          forcing UE to rebuild the proxy with fresh cell data. Mirrors
 *          `USeinNavDebugComponent`'s behavior.
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

	TWeakObjectPtr<USeinFogOfWar> SubscribedFog;
	FDelegateHandle FogMutatedHandle;
};
