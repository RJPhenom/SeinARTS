/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDecalFormationPreviewActor.h
 * @brief   Deferred-decal render backend for the destination preview. Overrides the
 *          element hooks on ASeinFormationPreviewActor with a pool of UDecalComponents.
 *
 *          Use this when the preview must CONFORM to uneven terrain (a projected decal
 *          drapes over slopes; a flat mesh quad does not). The trade-off: a deferred decal
 *          cannot write velocity for its OWN motion, so while the cursor drags the markers
 *          across the ground they SMEAR/ghost under TAA. Pair this backend with TSR (which
 *          rejects stale history by colour) if you need it. For a perfectly clean preview on
 *          flat-ish ground, prefer the default mesh backend (ASeinFormationPreviewActor).
 *
 *          Select via USeinARTSCoreSettings::FormationPreviewActorClass. PreviewMaterial
 *          must be a Deferred Decal material (null → engine DefaultDeferredDecalMaterial).
 */

#pragma once

#include "CoreMinimal.h"
#include "Preview/SeinFormationPreviewActor.h"
#include "SeinDecalFormationPreviewActor.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;

/**
 * Draws the destination/formation preview markers as decals projected onto the ground, so they
 * drape over hills and slopes instead of floating on a flat plane. Pick this when your maps have
 * uneven terrain; pick the mesh backend (Formation Preview Actor) when the ground is mostly flat.
 *
 * Uses DEFERRED DECAL projection: each marker is a UDecalComponent whose projection box is cast
 * straight down onto the scene, painting the preview material onto whatever ground geometry it
 * touches. This is what lets the marker conform to terrain. The trade-off is a deferred decal
 * cannot write motion velocity for its own movement, so while you drag the markers across the
 * ground they smear/ghost under TAA; pair this backend with TSR (which rejects stale history by
 * colour) if that bothers you. ProjectionDepthUU sets how far each decal box extends vertically in
 * world units (centered on the placement position) — keep it small (~64) so decals hit the ground
 * only and don't bleed onto overhead structures. Selected via the Formation Preview Actor Class
 * setting; the assigned preview material must be a Deferred Decal domain material (null falls back
 * to the engine's default deferred-decal material).
 */
UCLASS(Blueprintable, NotPlaceable, meta = (DisplayName = "Formation Preview Actor (Decal)"))
class SEINARTSFRAMEWORK_API ASeinDecalFormationPreviewActor : public ASeinFormationPreviewActor
{
	GENERATED_BODY()

public:
	/** Total projection depth in world units — how far each decal box extends vertically
	 *  (centered on the placement position). Keep small (~64) so the decal hits ground
	 *  geometry only and doesn't bleed onto overhead structures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview",
		meta = (ClampMin = "1.0", UIMin = "16.0", UIMax = "256.0"))
	float ProjectionDepthUU = 64.f;

protected:
	virtual void EnsureElementCount_Implementation(int32 Count) override;
	virtual void UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU) override;
	virtual void SetElementVisible_Implementation(int32 Index, bool bVisible) override;
	virtual int32 NumElements_Implementation() const override;

	/** Decal-domain fallback (engine DefaultDeferredDecalMaterial) instead of the base's
	 *  mesh fallback. */
	virtual UMaterialInterface* ResolvePreviewMaterial() const override;

	/** Decal pool — parallel to DecalMIDs. Components past the active count are hidden. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> Decals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DecalMIDs;
};
