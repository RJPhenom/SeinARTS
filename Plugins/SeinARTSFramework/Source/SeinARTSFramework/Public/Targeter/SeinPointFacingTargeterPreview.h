/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPointFacingTargeterPreview.h
 * @brief   Phase 3 preview for USeinPointFacingTargeterSpec — hologram of
 *          a building actor, drag-rotated and cell-snapped, with validity
 *          tinting for placement feedback.
 *
 *          Mesh resolution (multi-mesh per building, in priority order):
 *            1. Spec's PreviewMeshOverride (designer-set per-ability) —
 *               renders as a single mesh on the root HologramMesh.
 *            2. ALL UStaticMeshComponent on the BuildingClass CDO + SCS —
 *               cloned as dynamic child mesh components, each preserving the
 *               source's authored relative transform (location, rotation,
 *               scale). This is the typical case: a building Blueprint
 *               composed of several primitives renders 1:1 in the hologram.
 *            3. Whatever mesh (if any) the designer set on HologramMesh in
 *               the BP CDO — fallback when neither override nor CDO walk
 *               provides anything.
 *
 *          Every visible mesh component is wrapped in a dynamic material
 *          instance of GhostMaterial. The material is expected to expose a
 *          "TintColor" Vector parameter — the preview pushes per-frame
 *          validity colors into it (green/yellow/red).
 *
 *          During WaitingForCapture the preview tracks the cursor. On
 *          Dragging it locks position to the press-down anchor and rotates
 *          yaw to face from anchor toward cursor (snapped to spec's
 *          RotationStepDegrees).
 */

#pragma once

#include "CoreMinimal.h"
#include "Targeter/SeinTargeterPreview.h"
#include "SeinPointFacingTargeterPreview.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UMeshComponent;
class UMaterialInterface;

UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinPointFacingTargeterPreview : public ASeinTargeterPreview
{
	GENERATED_BODY()

public:
	ASeinPointFacingTargeterPreview();

	/** Translucent ghost material applied to every mesh component on the
	 *  hologram. Designers set this in the BP subclass or via the spec's
	 *  PreviewClass override. Material is expected to expose a "TintColor"
	 *  Vector parameter for validity tinting (green = Valid, yellow = Warning,
	 *  red = Blocked) — same convention as ASeinPointTargeterPreview's decal
	 *  material.
	 *
	 *  Applied to every slot of every visible mesh, so multi-material buildings
	 *  ghost cleanly across all submaterials. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	virtual void InitializePreview(USeinTargeterSpec* InSpec, float InAreaRadiusWorld) override;

protected:
	/** Root mesh component. Doubles as a transform anchor (multi-mesh case)
	 *  AND a single-mesh holder (override / fallback cases). When multi-mesh
	 *  is built from BuildingClass CDO, this component's own static mesh is
	 *  cleared and DynamicMeshes attach as children. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	TObjectPtr<UStaticMeshComponent> HologramMesh;

	/** Runtime-cloned mesh components that mirror the BuildingClass CDO's
	 *  UMeshComponent set (both static + skeletal — game teams using either
	 *  for buildings get the right preview type per-source). Each preserves
	 *  the source's relative transform + ghost material. Empty in the
	 *  single-mesh-override path. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	TArray<TObjectPtr<UMeshComponent>> DynamicMeshes;

	virtual void OnPreviewUpdated_Implementation() override;

private:
	/** Build all hologram mesh components from the spec — either single-mesh
	 *  override or multi-mesh CDO walk. Called from InitializePreview when
	 *  Spec is set, BEFORE BeginPlay (deferred-spawn pattern). */
	void BuildHologramMeshes();

	/** Wrap GhostMaterial in a dynamic instance for each material slot on the
	 *  given component, so per-instance TintColor changes don't leak into the
	 *  source asset. Operates on UMeshComponent base — works for both static
	 *  and skeletal mesh clones. No-op if either GhostMaterial or Comp is null. */
	void ApplyGhostMaterialToComponent(UMeshComponent* Comp);

	/** Push the given color into "TintColor" on every dynamic material instance
	 *  on the given component. Silent no-op if material doesn't expose the
	 *  parameter (UE handles unknown param names gracefully). UMeshComponent
	 *  base so the same call covers both static + skeletal clones. */
	void TintComponent(UMeshComponent* Comp, const FLinearColor& Color);
};
