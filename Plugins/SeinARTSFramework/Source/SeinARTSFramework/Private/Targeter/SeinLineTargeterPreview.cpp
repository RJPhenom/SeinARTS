/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLineTargeterPreview.cpp
 * @brief   Line/corridor targeter preview: active-segment rectangle decal with
 *          validity tinting plus accumulated committed-segment decals.
 */

#include "Targeter/SeinLineTargeterPreview.h"
#include "Abilities/SeinTargeterSpec.h"
#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

ASeinLineTargeterPreview::ASeinLineTargeterPreview()
{
	SegmentDecal = CreateDefaultSubobject<UDecalComponent>(TEXT("SegmentDecal"));
	RootComponent = SegmentDecal;
	// Project downward onto the ground (UE decals project along +X by default).
	SegmentDecal->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	SegmentDecal->SetVisibility(false);
}

void ASeinLineTargeterPreview::BeginPlay()
{
	Super::BeginPlay();
	// Same dynamic-instance wrap as the point preview so per-instance validity
	// tinting never mutates the source asset.
	if (SegmentDecal)
	{
		if (UMaterialInterface* SourceMat = SegmentDecal->GetDecalMaterial())
		{
			SegmentDecal->SetDecalMaterial(
				UMaterialInstanceDynamic::Create(SourceMat, this));
		}
	}
}

float ASeinLineTargeterPreview::ResolveHalfWidth() const
{
	const USeinLineTargeterSpec* LineSpec = Cast<USeinLineTargeterSpec>(Spec);
	const float SpecWidth = LineSpec ? LineSpec->Width.ToFloat() : 0.0f;
	return SpecWidth > 0.0f ? SpecWidth * 0.5f : DefaultLineHalfWidth;
}

void ASeinLineTargeterPreview::LayoutSegmentDecal(UDecalComponent& Decal,
	const FVector& StartWorld, const FVector& EndWorld) const
{
	const FVector Delta = EndWorld - StartWorld;
	const float Length = Delta.Size2D();
	// With the -90° pitch projecting the decal downward, decal-local Y runs
	// along the actor's yaw heading on the ground and local Z runs across it.
	const float Yaw = Length > KINDA_SMALL_NUMBER
		? Delta.Rotation().Yaw
		: 0.0f;
	Decal.SetWorldLocation((StartWorld + EndWorld) * 0.5f);
	Decal.SetWorldRotation(FRotator(-90.0f, Yaw, 0.0f));
	Decal.DecalSize = FVector(
		DecalHeight,
		FMath::Max(Length * 0.5f, 1.0f),
		ResolveHalfWidth());
}

void ASeinLineTargeterPreview::OnPreviewUpdated_Implementation()
{
	if (!SegmentDecal) return;

	// No anchor yet (waiting for the first press/click) — nothing to stretch;
	// hide the active segment. The base class already parked the actor at the
	// cursor, which keeps any BP-added cursor marker live.
	if (CurrentDragAnchorWorld.IsNearlyZero())
	{
		SegmentDecal->SetVisibility(false);
		return;
	}

	SegmentDecal->SetVisibility(true);
	LayoutSegmentDecal(*SegmentDecal, CurrentDragAnchorWorld, CurrentCursorWorld);

	if (UMaterialInstanceDynamic* DynMat =
		Cast<UMaterialInstanceDynamic>(SegmentDecal->GetDecalMaterial()))
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

void ASeinLineTargeterPreview::OnPointCaptured_Implementation(
	const FVector& StartWorld, const FVector& EndWorld)
{
	// Freeze the committed segment as its own decal so multi-segment sessions
	// (trench networks) keep earlier segments visible while the next one is
	// authored. Shares the SegmentDecal's source material with a neutral tint.
	if (!SegmentDecal) return;

	UDecalComponent* Committed = NewObject<UDecalComponent>(this);
	if (!Committed) return;
	// Absolute transform: the preview actor rides the cursor every tick, and a
	// committed segment must stay frozen where it was placed.
	Committed->SetAbsolute(true, true, true);
	Committed->RegisterComponent();
	Committed->AttachToComponent(
		RootComponent, FAttachmentTransformRules::KeepWorldTransform);
	if (UMaterialInterface* SourceMat = SegmentDecal->GetDecalMaterial())
	{
		UMaterialInstanceDynamic* DynMat =
			UMaterialInstanceDynamic::Create(SourceMat, this);
		DynMat->SetVectorParameterValue(TEXT("TintColor"), FLinearColor::White);
		Committed->SetDecalMaterial(DynMat);
	}
	LayoutSegmentDecal(*Committed, StartWorld, EndWorld);
	CommittedSegmentDecals.Add(Committed);
}
