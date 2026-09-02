/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinISMFormationPreviewActor.h
 * @brief   Instanced-Static-Mesh render backend for the destination preview. Renders the
 *          whole formation as instances of ONE UInstancedStaticMeshComponent — a single draw
 *          call — so it scales to very large formations far better than one component per
 *          element. Like the base mesh backend it does not ghost under TAA.
 *
 *          TINTING: an ISM cannot carry a per-instance MID, so the quality tint is written to
 *          per-instance custom-data floats 0–3 (R,G,B,A) instead of TintParameterName. For
 *          tinting to show, PreviewMaterial must read those — add a `PerInstanceCustomData`
 *          node (or GetPerInstanceCustomData 0..3) and feed it as the colour. Without it the
 *          preview still renders, just with the material's intrinsic colour (neutral default
 *          look — fine when no quality provider is bound). The ring's shape is owned by the
 *          material; per-member radius is handled by instance scale.
 *
 *          Select via a unit's USeinFormationPreviewComponent, or project-wide via
 *          USeinARTSCoreSettings::FormationPreviewActorClass.
 */

#pragma once

#include "CoreMinimal.h"
#include "Preview/SeinFormationPreviewActor.h"
#include "SeinISMFormationPreviewActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * Draws the destination/formation preview — the little markers that show where your units will
 * stand when you give a move order — as one big batch of identical meshes. Because every marker
 * is an instance of a single mesh component it renders in ONE draw call, so it stays cheap even
 * when the formation is huge. Like the other preview backends it does not smear or ghost under
 * temporal anti-aliasing (TAA).
 *
 * Uses INSTANCED-STATIC-MESH rendering: the whole formation is drawn as instances of one
 * UInstancedStaticMeshComponent rather than a separate component per marker, which is what lets
 * it scale to very large formations. The marker's ring shape comes from the material; each
 * member's radius is applied as per-instance scale.
 *
 * Tinting note: an instanced-static-mesh component cannot carry a per-instance material, so the
 * quality tint is written to per-instance custom-data floats 0-3 (as R, G, B, A) instead of a
 * material tint parameter. For the tint to appear, the preview material must read those floats —
 * add a PerInstanceCustomData node (or GetPerInstanceCustomData 0..3) and feed it into the
 * colour. Without that wiring the preview still renders, just in the material's own colour (a
 * neutral default that looks fine when no quality provider is bound).
 *
 * Select this backend via the Formation Preview Actor Class setting.
 */
UCLASS(Blueprintable, NotPlaceable, meta = (DisplayName = "Formation Preview Actor (Instanced Mesh)"))
class SEINARTSFRAMEWORK_API ASeinISMFormationPreviewActor : public ASeinFormationPreviewActor
{
	GENERATED_BODY()

protected:
	virtual void EnsureElementCount_Implementation(int32 Count) override;
	virtual void UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU) override;
	virtual void SetElementVisible_Implementation(int32 Index, bool bVisible) override;
	virtual int32 NumElements_Implementation() const override;
	virtual void CommitElements_Implementation(int32 ActiveCount) override;

	/** The single instanced-mesh component holding every preview element. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> ISMComp = nullptr;

	/** Shared MID wrapping the look material on ISMComp (per-instance tint is custom data). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ISMMID = nullptr;
};
