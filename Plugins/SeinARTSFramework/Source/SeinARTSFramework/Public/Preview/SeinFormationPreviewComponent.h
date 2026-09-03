/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationPreviewComponent.h
 * @author       RJ Macklem
 * @created      02 Sep 2026
 * @latest       02 Sep 2026
 * @brief        Render-side opt-in for the on-ground destination preview: adding
 *               this component to a unit (or squad) Blueprint is what makes it
 *               draw destination markers — there is no project-level enable
 *               switch. Its Preview Actor Class picks the renderer.
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
#include "Preview/SeinFormationPreviewActor.h"
#include "SeinFormationPreviewComponent.generated.h"

/**
 * Opts the owning unit into the on-ground destination preview — the ghost markers showing where
 * it will stand if the current move order is issued. Add it to a unit Blueprint to draw markers
 * for that unit; leave it off for units where the marker is noise (ambient/scripted units,
 * always-mobile scouts). On a squad's actor Blueprint it opts the whole squad in at once and its
 * renderer applies to every member; without it, each member's own component decides individually.
 *
 * Preview Actor Class picks the renderer for this unit's markers (None = the project default from
 * Formation Preview Actor Class in SeinARTS settings, else the framework's mesh-quad renderer).
 * Members of one selection may resolve to different renderers; markers are drawn grouped per
 * renderer class, so instanced backends still batch per unit type.
 */
UCLASS(ClassGroup = (SeinARTS), meta = (BlueprintSpawnableComponent, DisplayName = "Formation Preview Component"))
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
};
