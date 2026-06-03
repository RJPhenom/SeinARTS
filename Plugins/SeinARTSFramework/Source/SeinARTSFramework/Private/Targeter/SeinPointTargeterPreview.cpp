/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinPointTargeterPreview.cpp
 * @brief   Phase 1 point-targeter preview: cursor decal with optional AoE
 *          radius ring + validity tinting.
 */

#include "Targeter/SeinPointTargeterPreview.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinTargeterPreview, Log, All);

ASeinPointTargeterPreview::ASeinPointTargeterPreview()
{
	RingDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("RingDecal"));
	RootComponent = RingDecal;
	// Project the decal downward so it lands on the ground regardless of
	// the actor's exact Z. Default UE decal projects forward (+X), so rotate.
	RingDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
}

void ASeinPointTargeterPreview::BeginPlay()
{
	Super::BeginPlay();

	// Wrap the RingDecal's already-assigned material in a dynamic instance so
	// per-instance validity tinting (TintColor parameter writes) don't leak
	// back to the source asset. Designer sets the material on the RingDecal
	// component directly in the BP subclass — single slot, no duplicate
	// "DecalMaterial" actor field.
	if (RingDecal)
	{
		if (UMaterialInterface* SourceMat = RingDecal->GetDecalMaterial())
		{
			UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(SourceMat, this);
			RingDecal->SetDecalMaterial(DynMat);
		}
		else
		{
			UE_LOG(LogSeinTargeterPreview, Verbose,
				TEXT("ASeinPointTargeterPreview spawned without a decal material on RingDecal — preview will be invisible. Set the Decal Material on the RingDecal component in your BP subclass."));
		}
	}

	// Decal extents: half-X is the projection depth (height), half-Y / half-Z
	// are the on-ground footprint. AreaRadiusWorld of zero falls back to the
	// configured default point radius.
	const float Radius = AreaRadiusWorld > 0.0f ? AreaRadiusWorld : DefaultPointRadius;
	if (RingDecal)
	{
		RingDecal->DecalSize = FVector(DecalHeight, Radius, Radius);
	}
}

void ASeinPointTargeterPreview::OnPreviewUpdated_Implementation()
{
	if (!RingDecal) return;

	// Validity → tint. Read the dynamic instance, push a "TintColor" parameter.
	// Material is expected to expose this; if not, the call is a silent no-op
	// (UE handles unknown parameter names gracefully).
	UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(RingDecal->GetDecalMaterial());
	if (DynMat)
	{
		FLinearColor Tint = FLinearColor::White;
		switch (CurrentValidity)
		{
			case ESeinTargeterValidity::Valid:   Tint = FLinearColor(0.2f, 1.0f, 0.3f, 1.0f); break;
			case ESeinTargeterValidity::Warning: Tint = FLinearColor(1.0f, 0.85f, 0.2f, 1.0f); break;
			case ESeinTargeterValidity::Blocked: Tint = FLinearColor(1.0f, 0.2f, 0.2f, 1.0f); break;
		}
		DynMat->SetVectorParameterValue(TEXT("TintColor"), Tint);
	}
}
