/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewActor.h
 * @brief   Destination-preview renderer ("where will my selection land if I order
 *          here"). One instance per local player, lifecycle owned by
 *          USeinFormationPreviewSubsystem.
 *
 *          RENDER-BACKEND SEAM. This base class owns everything backend-agnostic —
 *          the SetPositions/HideAll API, the per-element change guards, and the
 *          quality→tint resolution — and delegates the actual drawing to a small
 *          set of `virtual` element hooks (EnsureElementCount / UpdateElement /
 *          SetElementVisible / NumElements / CommitElements). The base's OWN
 *          implementation of those hooks renders a pool of flat **mesh quads**,
 *          which is the recommended default: a moving mesh writes velocity (or uses
 *          a masked material that does), so it does NOT ghost under TAA the way a
 *          moving deferred decal does.
 *
 *          Other render styles are subclasses that override the hooks:
 *            - ASeinDecalFormationPreviewActor — deferred-decal pool (conforms to
 *              terrain; ghosts under TAA while the cursor drags — use with TSR).
 *            - ASeinISMFormationPreviewActor   — one InstancedStaticMesh, per-instance
 *              custom-data tint; one draw call, scales to huge formations.
 *          A project can author a fully custom backend in C++ (or Blueprint, by
 *          overriding the BlueprintNativeEvent hooks) — e.g. a Niagara-driven look.
 *          USeinARTSCoreSettings::FormationPreviewActorClass picks the class.
 *
 *          Pure presentation — never mutates sim state. The quality-tint map is a
 *          generic FGameplayTag→color table; the framework ships it EMPTY (neutral
 *          preview) and a project (or the Cover extension's preview BP) populates it.
 *          (Property names retain the historic "Cover" spelling so the existing
 *          preview BP's authored values survive.)
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SeinFormationPreviewActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Draws the destination preview for a move order — the ghost markers showing where your selected
 * units will stand if you order them here. One lives per local player, and this is the renderer
 * picked out of the box.
 *
 * This is the base of the render-backend seam: it owns everything drawing-style-agnostic (the
 * SetPositions / HideAll API, the per-element change guards that skip redraws when nothing moved,
 * and the quality-tag-to-tint color resolution) and delegates the actual drawing to a small set of
 * overridable element hooks (EnsureElementCount, UpdateElement, SetElementVisible, NumElements,
 * CommitElements). Its own hooks render a pool of flat mesh quads, which is the recommended default:
 * a moving mesh writes velocity (or uses a masked material that does), so it does NOT ghost under
 * Temporal Anti-Aliasing the way a moving deferred decal does while you drag the cursor.
 *
 * Other looks are subclasses that override those hooks: the decal backend renders a pool of deferred
 * decals that conform to terrain (but ghost under TAA — pair with TSR); the ISM backend uses one
 * Instanced Static Mesh with per-instance custom-data tint, a single draw call that scales to huge
 * formations. A project can author a fully custom backend in C++ or Blueprint (the element hooks are
 * BlueprintNativeEvents) — e.g. a Niagara-driven look. The Formation Preview Actor Class setting
 * picks which class is used. This is pure presentation and never mutates sim state. The
 * quality-tint map is a generic gameplay-tag-to-color table; the framework ships it empty (a neutral
 * preview) and a project — or the Cover extension's preview Blueprint — populates it. The tint
 * property names retain the historic "Cover" spelling so existing authored preview values survive.
 */
UCLASS(Blueprintable, NotPlaceable, meta = (DisplayName = "Formation Preview Actor (Mesh)"))
class SEINARTSFRAMEWORK_API ASeinFormationPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ASeinFormationPreviewActor();

	/** Material rendered on each preview element. Designers subclass in Blueprint and
	 *  set this in the BP CDO. For the default MESH backend this must be a Surface
	 *  material (Unlit + Masked or Translucent is typical); for the Decal backend it
	 *  must be a Deferred Decal material. Null = a backend-appropriate engine fallback
	 *  so something renders out of the box. (Formerly "DecalMaterial".) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UMaterialInterface> PreviewMaterial = nullptr;

	/** Mesh used by the mesh-style backends (base mesh + ISM). Null = the engine unit
	 *  Plane (/Engine/BasicShapes/Plane), a 100uu quad scaled to the footprint. Swap for
	 *  a custom ring/disc mesh to change the silhouette. Ignored by the Decal backend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UStaticMesh> PreviewMesh = nullptr;

	/** Full size of each element's ground footprint in world units (square). Set to the
	 *  diameter you want — e.g. 128 cm for a roughly unit-sized marker. Used when the
	 *  formation does NOT supply a per-member radius; otherwise the per-member footprint
	 *  diameter wins. The material draws whatever shape it wants inside this square. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview",
		meta = (ClampMin = "1.0", UIMin = "32.0", UIMax = "512.0"))
	float GroundSizeUU = 128.f;

	/** Vertical offset added to each position — lifts the marker slightly above the
	 *  ground point to avoid z-fighting with terrain. Tune per-project. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	float ZOffsetUU = 4.f;

	/** Material vector parameter that receives the per-element quality tint. The look
	 *  material should expose a Vector Parameter named this and use it as a color
	 *  multiplier. Safe no-op if absent. (The ISM backend cannot use a per-instance MID,
	 *  so it writes the tint to per-instance custom data floats 0–3 instead.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview|Quality")
	FName TintParameterName = TEXT("Tint");

	/** Tint applied where there is no quality tag for the cell (or the tag isn't in
	 *  CoverQualityTints). White (1,1,1,1) is a no-op multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview|Quality")
	FLinearColor NoCoverTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** Quality tag → tint color. The preview subsystem fills a per-position quality tag
	 *  (via USeinWorldSubsystem::PreviewQualityProvider — e.g. the Cover extension supplies
	 *  cover quality) and this maps it to a tint. Missing tags fall back to NoCoverTint.
	 *  SHIPS EMPTY in the framework (neutral preview). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview|Quality",
		meta = (ForceInlineRow))
	TMap<FGameplayTag, FLinearColor> CoverQualityTints;

	// Public API
	// ====================================================================================================

	/** Place N elements at the given world positions and show them. Lazily grows the pool
	 *  to fit; elements past Positions.Num() are hidden in place (not destroyed).
	 *
	 *  `Qualities` is an optional parallel array of per-position quality tags for color
	 *  coding. Empty / mismatched entries fall back to NoCoverTint.
	 *
	 *  `Radii` is an optional parallel array of per-position footprint radii (world cm):
	 *  each element's quad/decal scales to that footprint (so the material's ring, drawn in
	 *  UV space, sizes with it). Empty / mismatched entries fall back to GroundSizeUU. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Preview")
	void SetPositions(const TArray<FVector>& WorldPositions, const TArray<FGameplayTag>& CoverQualities, const TArray<float>& Radii);

	/** Hide every element in place without destroying it. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Preview")
	void HideAll();

protected:
	// ── Render-backend hooks ──────────────────────────────────────────────────────────
	// Override these in a subclass to change the render style. The base implements them
	// as a pool of flat mesh quads. SetPositions drives them; it guards each element so
	// UpdateElement fires only when an element's position/tint/radius actually changes.

	/** Grow the render pool/instances so at least Count elements exist. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Preview|Backend")
	void EnsureElementCount(int32 Count);
	virtual void EnsureElementCount_Implementation(int32 Count);

	/** Place + style element Index. Called only when its inputs changed. RadiusUU > 0 sizes
	 *  the element to that footprint DIAMETER; <= 0 → uniform GroundSizeUU. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Preview|Backend")
	void UpdateElement(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU);
	virtual void UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU);

	/** Show / hide element Index. */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Preview|Backend")
	void SetElementVisible(int32 Index, bool bVisible);
	virtual void SetElementVisible_Implementation(int32 Index, bool bVisible);

	/** Current pool size (number of allocated elements). */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Preview|Backend")
	int32 NumElements() const;
	virtual int32 NumElements_Implementation() const;

	/** Called once after a SetPositions pass has updated/hidden every element; ActiveCount
	 *  is the number now visible. Per-component backends no-op; batched backends (ISM) flush
	 *  their instance buffers here (UpdateElement uses bMarkDirty=false for batching). */
	UFUNCTION(BlueprintNativeEvent, Category = "SeinARTS|Preview|Backend")
	void CommitElements(int32 ActiveCount);
	virtual void CommitElements_Implementation(int32 ActiveCount) {}

	// ── Shared helpers ────────────────────────────────────────────────────────────────

	/** Resolve a quality tag to its configured tint, falling back to NoCoverTint when the
	 *  tag is invalid or absent from the map. */
	FLinearColor ResolveTintForQuality(const FGameplayTag& QualityTag) const;

	/** Square footprint side length for a given per-member radius (RadiusUU > 0 → diameter
	 *  2*RadiusUU; else the uniform GroundSizeUU). Shared by every mesh-style backend. */
	float ResolveFootprintSize(float RadiusUU) const;

	/** Resolve the look material (PreviewMaterial, else a backend-appropriate fallback).
	 *  Base mesh backend returns an unlit engine fallback; the Decal backend overrides it. */
	virtual UMaterialInterface* ResolvePreviewMaterial() const;

	/** Resolve the quad mesh (PreviewMesh, else the engine unit Plane). */
	UStaticMesh* ResolvePreviewMesh() const;

	// ── Base MESH backend state (flat quad pool) ──────────────────────────────────────

	/** Mesh-quad pool — grows up to the largest formation seen this session, reused across
	 *  selection changes. Components past the active count are SetVisibility(false). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> MeshPool;

	/** Per-element Material Instance Dynamic, parallel to MeshPool (one per element so each
	 *  cell's tint can differ). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MeshMIDs;

private:
	// Per-element change guards (backend-agnostic) — skip UpdateElement when nothing about
	// an element changed, avoiding per-frame render-thread churn on an idle cursor. Hiding an
	// element invalidates its guard so a later reuse always re-places.
	TArray<FVector>      LastWorldPositions;
	TArray<FLinearColor> LastTints;
	TArray<float>        LastRadii;
};
