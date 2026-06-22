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
