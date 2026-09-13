/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPointFacingTargeterPreview.h
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Compatibility mesh-preview preset for existing Blueprint parents.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#pragma once
#include "CoreMinimal.h"
#include "Targeter/SeinTargeterPreview.h"
#include "SeinPointFacingTargeterPreview.generated.h"
class UStaticMeshComponent;
class UMeshComponent;
class UMaterialInterface;
class USeinTargeterMeshComponent;

/** Mesh preset. Any capture gesture can instead use Targeter Preview with an optional mesh component. */
UCLASS(Blueprintable)
class SEINARTSFRAMEWORK_API ASeinPointFacingTargeterPreview : public ASeinTargeterPreview
{
	GENERATED_BODY()
public:
	ASeinPointFacingTargeterPreview();
	/** Legacy single material. Used with TintColor only when the mesh component has no validity materials. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AdvancedDisplay, Category="SeinARTS")
	TObjectPtr<UMaterialInterface> GhostMaterial;
	/** Configure visual sources and independent valid, warning, and blocked materials here. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS")
	TObjectPtr<USeinTargeterMeshComponent> MeshPreview;
protected:
	/** Retained single-mesh component for existing Blueprint references. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS") TObjectPtr<UStaticMeshComponent> HologramMesh;
	/** Retained view of generated meshes for existing Blueprint graphs. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="SeinARTS") TArray<TObjectPtr<UMeshComponent>> DynamicMeshes;
	virtual void PrepareVisuals() override;
	virtual void OnPreviewUpdated_Implementation() override;
};
