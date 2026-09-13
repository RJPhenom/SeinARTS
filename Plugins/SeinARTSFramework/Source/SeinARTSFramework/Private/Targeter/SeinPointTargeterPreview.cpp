/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file         SeinPointTargeterPreview.cpp
 * @author       RJ Macklem
 * @created      12 Sep 2026
 * @latest       12 Sep 2026
 * @brief        Adapts legacy ring authoring to the optional decal visual component.
 * @disclaimer   This code was generated in part with the assistance of an AI language model.
 */
#include "Targeter/SeinPointTargeterPreview.h"
#include "Targeter/SeinTargeterVisualComponent.h"
#include "Components/DecalComponent.h"

ASeinPointTargeterPreview::ASeinPointTargeterPreview()
{
	RingDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("RingDecal"));
	RingDecal->SetupAttachment(RootComponent);
	RingDecal->SetRelativeRotation(FRotator(-90, 0, 0));
	DecalPreview = CreateDefaultSubobject<USeinTargeterDecalComponent>(TEXT("DecalPreview"));
}
void ASeinPointTargeterPreview::PrepareVisuals()
{
	DecalPreview->BindLegacy(RingDecal);
	// Preserve legacy size overrides until an author configures the helper directly.
	if (DefaultPointRadius != 60) DecalPreview->Radius = DefaultPointRadius;
	if (DecalHeight != 200) DecalPreview->ProjectionDepth = DecalHeight;
}
void ASeinPointTargeterPreview::OnPreviewUpdated_Implementation() {}
