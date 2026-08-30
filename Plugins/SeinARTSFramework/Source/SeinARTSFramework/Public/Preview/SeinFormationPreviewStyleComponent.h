/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationPreviewStyleComponent.h
 * @author       RJ Macklem
 * @created      29 Aug 2026
 * @latest       29 Aug 2026
 * @brief        Per-unit authoring surface for the destination preview: an optional,
 *               data-only actor component designers add to a unit Blueprint to give
 *               that unit's preview marker its own look.
 *
 *               Render-side only — this is an ordinary actor component on the visual
 *               actor, NOT a sim component payload, so nothing here participates in
 *               canonical state, hashing, or the config fingerprint. The preview
 *               subsystem reads it when the unit is part of a previewed selection
 *               (cached per selection) and hands the resolved style to the render
 *               backend picked by the Formation Preview Actor Class setting. Units
 *               without this component keep the backend's project-wide defaults.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Preview/SeinFormationPreviewTypes.h"
#include "SeinFormationPreviewStyleComponent.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * Gives this unit its own destination-preview marker look. Add it to a unit Blueprint and set only
 * the fields you want to change — anything left unset keeps the project-wide look from the preview
 * backend, and units without this component are unaffected. Typical use: a wide rectangle marker
 * for vehicles while infantry keep the default disc.
 *
 * Pure data, render-side only: the preview subsystem reads these fields when the unit is part of a
 * previewed selection and passes them to the preview render backend. It never touches simulation
 * state, and it does not change WHERE markers appear (destinations always come from the shared
 * formation resolver) — only how this unit's marker is drawn.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Formation Preview Style Component"))
class SEINARTSFRAMEWORK_API USeinFormationPreviewStyleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinFormationPreviewStyleComponent();

	/** Marker mesh for this unit, used by the mesh and instanced-mesh preview backends.
	 *  None = the backend's default mesh. Author it with a 100 uu footprint (like the
	 *  engine Plane) — the backend scales it to the unit's footprint assuming that base
	 *  size. Ignored by the decal backend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UStaticMesh> MarkerMesh = nullptr;

	/** Marker material for this unit. None = the backend's default material. Must match
	 *  the active backend's material domain: Surface for the mesh and instanced-mesh
	 *  backends, Deferred Decal for the decal backend. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UMaterialInterface> MarkerMaterial = nullptr;

	/** Colour multiplied into this unit's marker tint, on top of any quality tint (for
	 *  example cover quality). White (1,1,1,1) = no change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	FLinearColor StyleTint = FLinearColor::White;

	/** Fixed marker footprint for this unit — the full ground diameter in world units.
	 *  0 = no override: the marker sizes to the unit's formation footprint as usual. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "512.0"))
	float MarkerSizeUU = 0.f;

	/** Free-form tag a custom preview backend can branch on (e.g. one look per unit
	 *  family). The shipped backends ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	FGameplayTag StyleTag;

	/** Pack this component's authored fields into the per-element style the preview
	 *  render backend consumes, stamped with the given member handle. */
	FSeinFormationPreviewElementStyle BuildElementStyle(FSeinEntityHandle MemberHandle) const;
};
