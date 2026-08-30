/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinISMFormationPreviewActor.h
 * @brief   Instanced-Static-Mesh render backend for the destination preview. Renders the
 *          formation as instances of one UInstancedStaticMeshComponent PER DISTINCT LOOK
 *          (mesh + material pair) — one draw call per look, so a uniformly-styled
 *          formation stays a single draw call and per-unit style overrides cost one
 *          extra draw call per distinct look, not per marker. Like the base mesh backend
 *          it does not ghost under TAA.
 *
 *          TINTING: an ISM cannot carry a per-instance MID, so the tint (quality tint ×
 *          style tint) is written to per-instance custom-data floats 0–3 (R,G,B,A)
 *          instead of TintParameterName. For tinting to show, the look material must
 *          read those — add a `PerInstanceCustomData` node (or GetPerInstanceCustomData
 *          0..3) and feed it as the colour. Without it the preview still renders, just
 *          with the material's intrinsic colour (neutral default look — fine when no
 *          quality provider is bound). The ring's shape is owned by the material;
 *          per-member radius is handled by instance scale.
 *
 *          Select via USeinARTSCoreSettings::FormationPreviewActorClass.
 */

#pragma once

#include "CoreMinimal.h"
#include "Preview/SeinFormationPreviewActor.h"
#include "SeinISMFormationPreviewActor.generated.h"

class UInstancedStaticMeshComponent;

/**
 * One instanced-mesh group of the ISM preview backend: the component drawing every marker
 * that shares one look (mesh + material pair), plus the look it was created for.
 */
USTRUCT()
struct FSeinISMFormationPreviewGroup
{
	GENERATED_BODY()

	/** The instanced-mesh component batching this look's markers. */
	UPROPERTY(Transient)
	TObjectPtr<UInstancedStaticMeshComponent> Comp = nullptr;

	/** Resolved marker mesh this group renders. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	/** Resolved look material this group renders. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material = nullptr;
};

/**
 * Draws the destination/formation preview — the little markers that show where your units will
 * stand when you give a move order — as big batches of instanced meshes. Every marker sharing one
 * look (mesh + material) renders as instances of a single mesh component, ONE draw call per look,
 * so it stays cheap even when the formation is huge. Like the other preview backends it does not
 * smear or ghost under temporal anti-aliasing (TAA).
 *
 * Uses INSTANCED-STATIC-MESH rendering grouped by look: with no per-unit style overrides the whole
 * formation is one draw call; units whose Formation Preview Style Component overrides the marker
 * mesh or material add one draw call per distinct look, not per marker. The marker's ring shape
 * comes from the material; each member's radius is applied as per-instance scale.
 *
 * Tinting note: an instanced-static-mesh component cannot carry a per-instance material, so the
 * tint (quality tint times style tint) is written to per-instance custom-data floats 0-3 (as R, G,
 * B, A) instead of a material tint parameter. For the tint to appear, each look material must read
 * those floats — add a PerInstanceCustomData node (or GetPerInstanceCustomData 0..3) and feed it
 * into the colour. Without that wiring the preview still renders, just in the material's own colour
 * (a neutral default that looks fine when no quality provider is bound).
 *
 * Select this backend via the Formation Preview Actor Class setting.
 */
UCLASS(Blueprintable, NotPlaceable, meta = (DisplayName = "Formation Preview Actor (Instanced Mesh)"))
class SEINARTSFRAMEWORK_API ASeinISMFormationPreviewActor : public ASeinFormationPreviewActor
{
	GENERATED_BODY()

protected:
	virtual void EnsureElementCount_Implementation(int32 Count) override;
	virtual void UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU, const FSeinFormationPreviewElementStyle& Style) override;
	virtual void SetElementVisible_Implementation(int32 Index, bool bVisible) override;
	virtual int32 NumElements_Implementation() const override;
	virtual void CommitElements_Implementation(int32 ActiveCount) override;

	/** Find the group rendering this mesh + material look, creating (and registering) its
	 *  instanced-mesh component on first use. Returns an index into Groups. */
	int32 FindOrCreateGroup(UStaticMesh* Mesh, UMaterialInterface* Material);

	/** One instanced-mesh component per distinct look seen this session. Never shrinks —
	 *  a look whose markers all vanish just holds zero instances. */
	UPROPERTY(Transient)
	TArray<FSeinISMFormationPreviewGroup> Groups;

private:
	/** Logical per-element state, index-aligned with the orchestrator's element indices.
	 *  Hooks write here; CommitElements uploads to the group components — incrementally
	 *  while the element→group mapping is stable (the cursor-drag hot path), or by
	 *  rebuilding every group's instances when grouping changed (selection/style changes). */
	struct FElementState
	{
		FTransform Xform = FTransform::Identity;
		FLinearColor Tint = FLinearColor::White;
		bool bVisible = false;
		bool bDirty = false;
		int32 GroupIndex = INDEX_NONE;
		int32 LocalInstanceIndex = INDEX_NONE;   // instance index within the group; INDEX_NONE while hidden
	};
	TArray<FElementState> Elements;

	/** True when an element changed group or visibility since the last commit — the next
	 *  CommitElements rebuilds group instance lists instead of updating in place. */
	bool bGroupingDirty = false;
};
