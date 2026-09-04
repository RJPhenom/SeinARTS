/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinFormationPreviewTypes.h
 * @author       RJ Macklem
 * @created      29 Aug 2026
 * @latest       03 Sep 2026
 * @brief        Value types shared by the destination-preview seam: the per-element
 *               style descriptor the preview subsystem resolves from each unit and
 *               hands to the render backend.
 *
 *               Render-side only — nothing here is simulation state. The style is
 *               resolved from the Navigation Renderer on the unit or its squad and
 *               flows through ASeinFormationPreviewActor's element hooks so backends
 *               can render per-unit marker looks.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Core/SeinEntityHandle.h"
#include "SeinFormationPreviewTypes.generated.h"

class UStaticMesh;
class UMaterialInterface;

/**
 * The per-marker style for one member of a destination preview — which unit the marker belongs to
 * and how that unit wants its marker to look. Every field except the member handle is an optional
 * override: unset fields fall back to the preview backend's own defaults, so a default-constructed
 * style renders exactly the project-wide look.
 *
 * The preview subsystem resolves one of these per selected member from its Navigation Renderer,
 * inheriting squad fields before member fields, and passes it through the preview actor's element
 * hooks. Custom backends can also branch on Style Tag or query the member handle for fully
 * adaptive per-unit rendering.
 */
USTRUCT(BlueprintType)
struct SEINARTSFRAMEWORK_API FSeinFormationPreviewElementStyle
{
	GENERATED_BODY()

	/** The member this marker previews the destination of. Always set by the preview
	 *  subsystem; invalid on a default-constructed style (e.g. an element with no
	 *  member information). */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Preview")
	FSeinEntityHandle MemberHandle;

	/** Marker mesh override for mesh-style backends. None = the backend's default mesh.
	 *  Author the mesh with a 100 uu footprint (like the engine Plane) — backends scale
	 *  it to the member's footprint assuming that base size. Ignored by the decal backend. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UStaticMesh> MarkerMesh = nullptr;

	/** Marker material override. None = the backend's default material. Must match the
	 *  backend's material domain: Surface for the mesh and instanced-mesh backends,
	 *  Deferred Decal for the decal backend. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Preview")
	TObjectPtr<UMaterialInterface> MarkerMaterial = nullptr;

	/** Colour multiplied into the marker's quality tint. White (1,1,1,1) = no change. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Preview")
	FLinearColor StyleTint = FLinearColor::White;

	/** Marker footprint override — the full ground diameter in world units. 0 (or less)
	 *  = no override: the marker sizes to the member's formation footprint as usual. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Preview")
	float MarkerSizeUU = 0.f;

	/** Free-form tag for custom preview backends to branch on (e.g. one backend look per
	 *  unit family). The shipped backends ignore it. */
	UPROPERTY(BlueprintReadWrite, Category = "SeinARTS|Preview")
	FGameplayTag StyleTag;

	bool operator==(const FSeinFormationPreviewElementStyle& Other) const
	{
		return MemberHandle == Other.MemberHandle
			&& MarkerMesh == Other.MarkerMesh
			&& MarkerMaterial == Other.MarkerMaterial
			&& StyleTint == Other.StyleTint
			&& MarkerSizeUU == Other.MarkerSizeUU
			&& StyleTag == Other.StyleTag;
	}

	bool operator!=(const FSeinFormationPreviewElementStyle& Other) const
	{
		return !(*this == Other);
	}
};
