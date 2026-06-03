/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPointTargeterPreview.h
 * @brief   Phase 1 default preview for USeinPointTargeterSpec — a decal at
 *          the cursor with an optional AoE radius ring.
 *
 *          Visualization is intentionally minimal for Phase 1: a single
 *          decal component sized to the AoE radius (or a default cursor-
 *          marker size when AreaRadius is zero). Designers swap to a richer
 *          BP subclass via the spec's PreviewClass field for per-ability
 *          visuals.
 *
 *          The decal material name + tint behavior in response to validity
 *          is exposed as configurable defaults — game teams override the
 *          material globally via project plugin settings (Phase 1.5+) or
 *          per-ability via the spec.
 */

#pragma once

#include "CoreMinimal.h"
#include "Targeter/SeinTargeterPreview.h"
#include "SeinPointTargeterPreview.generated.h"

class UDecalComponent;
class UMaterialInterface;

UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinPointTargeterPreview : public ASeinTargeterPreview
{
	GENERATED_BODY()

public:
	ASeinPointTargeterPreview();

	/** Decal radius when AreaRadius is zero (point-only preview). World units.
	 *
	 *  NOTE on material setup: the targeter preview's decal material is set
	 *  ON THE INHERITED `RingDecal` component directly — see the Components
	 *  panel → RingDecal → Decal Material. The framework wraps that material
	 *  in a MaterialInstanceDynamic at BeginPlay so it can push validity tint
	 *  per-instance without mutating the source asset. Designers using a
	 *  Substrate material should expose tinting via a parameter named
	 *  `TintColor` to receive the framework's validity tint feedback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	float DefaultPointRadius = 60.0f;

	/** Decal vertical extent — projection depth above + below the ground.
	 *  Set generously to handle slope variation; performance impact negligible
	 *  for one decal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	float DecalHeight = 200.0f;

protected:
	/** Decal component drawing the ring. Sized from AreaRadiusWorld (or
	 *  DefaultPointRadius when zero). Created in the constructor, configured
	 *  on InitializePreview. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS|Targeter")
	TObjectPtr<UDecalComponent> RingDecal;

	virtual void OnPreviewUpdated_Implementation() override;
	virtual void BeginPlay() override;
};
