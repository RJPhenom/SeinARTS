/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationPreviewComponent.h
 * @author       RJ Macklem
 * @created      02 Sep 2026
 * @latest       03 Sep 2026
 * @brief        Render-side opt-in for the on-ground destination preview: adding
 *               this component to a unit (or squad) Blueprint is what makes it
 *               draw destination markers — there is no project-level enable
 *               switch. Its Preview Actor Class picks the renderer, while its
 *               marker fields optionally override that renderer's visual defaults.
 *
 *               Pure presentation data on the visual actor, never a sim
 *               component: it does not enter ComponentData, canonical state,
 *               snapshots, or the config fingerprint. Destination computation
 *               (the formation layout dry-run and the frozen destination
 *               artifact the commit reuses) runs identically whether or not any
 *               selected unit carries this component — it only decides which of
 *               the already-computed markers are drawn, and with which renderer.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Preview/SeinFormationPreviewActor.h"
#include "SeinFormationPreviewComponent.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * Opts the owning unit into the on-ground destination preview — the ghost markers showing where
 * it will stand if the current move order is issued. Add it to a unit Blueprint to draw markers
 * for that unit; leave it off for units where the marker is noise (ambient/scripted units,
 * always-mobile scouts). On a squad's actor Blueprint it opts the whole squad in at once and its
 * renderer applies to every member; without it, each member's own component decides individually.
 *
 * Preview Actor Class picks the renderer for this unit's markers (None = the project default from
 * Formation Preview Actor Class in SeinARTS settings, else the framework's mesh-quad renderer).
 * The remaining fields optionally override that renderer's marker defaults. On a squad renderer,
 * those fields become defaults for its members; a renderer on an individual member overrides the
 * squad field-by-field. Members of one selection may resolve to different renderers; markers are
 * drawn grouped per renderer class, so instanced backends still batch per unit type.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "SeinARTS Navigation Renderer"))
class SEINARTSFRAMEWORK_API USeinFormationPreviewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USeinFormationPreviewComponent();

	/** Renderer drawing this unit's destination marker. None = the project default
	 *  (Formation Preview Actor Class in SeinARTS settings), falling back to the
	 *  framework mesh-quad renderer. Point it at a Formation Preview Actor subclass
	 *  (mesh / decal / instanced-mesh, or a Blueprint overriding the element hooks)
	 *  to restyle this unit type's marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview",
		meta = (DisplayName = "Preview Actor Class"))
	TSoftClassPtr<ASeinFormationPreviewActor> PreviewActorClass;

	/** Marker mesh used by mesh and instanced-mesh renderers. None inherits the
	 *  squad renderer's mesh, then the renderer backend's default. Author it with
	 *  a 100 uu footprint; the renderer scales it to the resolved marker size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UStaticMesh> MarkerMesh = nullptr;

	/** Marker material override. None inherits the squad renderer's material,
	 *  then the renderer backend's default. It must match the active renderer's
	 *  material domain: Surface for mesh renderers or Deferred Decal for decals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UMaterialInterface> MarkerMaterial = nullptr;

	/** Colour multiplied into the marker tint after any quality tint. White
	 *  inherits the squad renderer's tint, then means no additional tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	FLinearColor StyleTint = FLinearColor::White;

	/** Fixed marker footprint: the full ground diameter in world units. Zero
	 *  inherits the squad renderer's size, then uses the unit's formation
	 *  footprint when neither renderer supplies an override. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "512.0"))
	float MarkerSizeUU = 0.f;

	/** Free-form tag a custom renderer can branch on. An empty tag inherits the
	 *  squad renderer's tag. The shipped renderers ignore it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Preview")
	FGameplayTag StyleTag;

	/** Resolve this component's authored marker fields over an optional inherited
	 *  squad style and stamp the result with the member being rendered. */
	FSeinFormationPreviewElementStyle BuildElementStyle(
		FSeinEntityHandle MemberHandle,
		const FSeinFormationPreviewElementStyle* InheritedStyle = nullptr) const;
};
