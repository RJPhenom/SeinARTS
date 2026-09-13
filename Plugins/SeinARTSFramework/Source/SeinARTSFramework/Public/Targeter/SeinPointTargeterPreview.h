/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPointTargeterPreview.h
 * @author       RJ Macklem
 * @created      02 Jun 2026
 * @latest       12 Sep 2026
 * @brief        Compatibility decal preset. Configure new visuals on Decal Preview.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once

#include "CoreMinimal.h"
#include "Targeter/SeinTargeterPreview.h"
#include "SeinPointTargeterPreview.generated.h"

class UDecalComponent;
class UMaterialInterface;
class USeinTargeterDecalComponent;

UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinPointTargeterPreview : public ASeinTargeterPreview
{
	GENERATED_BODY()

public:
	ASeinPointTargeterPreview();
	/** Optional decal renderer. Configure validity materials and sizing on this component. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS")
	TObjectPtr<USeinTargeterDecalComponent> DecalPreview;

	/** Legacy fallback radius in world units. Prefer the Decal Preview component for new authoring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AdvancedDisplay, Category = "SeinARTS")
	float DefaultPointRadius = 60.0f;

	/** Decal vertical extent — projection depth above + below the ground.
	 *  Set generously to handle slope variation; performance impact negligible
	 *  for one decal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AdvancedDisplay, Category = "SeinARTS")
	float DecalHeight = 200.0f;

protected:
	/** Decal component drawing the ring. Sized from AreaRadiusWorld (or
	 *  DefaultPointRadius when zero). Created in the constructor, configured
	 *  on InitializePreview. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SeinARTS")
	TObjectPtr<UDecalComponent> RingDecal;

	virtual void OnPreviewUpdated_Implementation() override;
	virtual void PrepareVisuals() override;
};
