/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPointFacingTargeterPreview.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Adapts legacy hologram authoring to the optional mesh visual component.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Targeter/SeinPointFacingTargeterPreview.h"
#include "Targeter/SeinTargeterVisualComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Abilities/SeinTargeterSpec.h"

ASeinPointFacingTargeterPreview::ASeinPointFacingTargeterPreview()
{
	HologramMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HologramMesh"));
	HologramMesh->SetupAttachment(RootComponent);
	HologramMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HologramMesh->SetCanEverAffectNavigation(false);
	HologramMesh->SetCastShadow(false);
	MeshPreview = CreateDefaultSubobject<USeinTargeterMeshComponent>(TEXT("MeshPreview"));
}
void ASeinPointFacingTargeterPreview::PrepareVisuals()
{
	MeshPreview->BindLegacy(HologramMesh, GhostMaterial);
	if (const auto* Facing = Cast<USeinPointFacingTargeterSpec>(Spec))
	{
		if (MeshPreview->SourceClass.IsNull() && Context.ActorClass.IsNull()) MeshPreview->SourceClass = Facing->BuildingClass;
		if (MeshPreview->MeshOverride.IsNull()) MeshPreview->MeshOverride = Facing->PreviewMeshOverride;
	}
	// Make compatibility mesh getters available before Blueprint initialization.
	MeshPreview->InitializeVisual(Context);
	DynamicMeshes = MeshPreview->GeneratedMeshes;
}
void ASeinPointFacingTargeterPreview::OnPreviewUpdated_Implementation()
{
	// Legacy direct Update Preview callers still receive the prior anchor/rotation behavior.
	if (!bPresentationStarted && bFollowResolvedTarget)
	{
		if (bCaptureHasAnchor || !CurrentDragAnchorWorld.IsNearlyZero()) SetActorLocation(CurrentDragAnchorWorld);
		SetActorRotation(FRotator(0, CurrentDragYawDegrees, 0));
	}
}
