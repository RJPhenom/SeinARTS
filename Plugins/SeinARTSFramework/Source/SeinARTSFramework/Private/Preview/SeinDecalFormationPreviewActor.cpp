/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinDecalFormationPreviewActor.cpp
 * @brief   Deferred-decal render backend for the destination preview (see header for the
 *          terrain-conform vs. TAA-ghosting trade-off). This is the original preview render
 *          path, preserved as an opt-in backend after the base switched to mesh quads.
 */

#include "Preview/SeinDecalFormationPreviewActor.h"

#include "Components/DecalComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinDecalPreview, Log, All);

namespace SeinDecalPreviewLocal
{
	/** Engine stock deferred-decal material so the preview renders SOMETHING before the
	 *  designer assigns a decal material. It has no Tint/OuterRad parameters, so styling is
	 *  a no-op while it's in use. */
	static UMaterialInterface* GetFallbackDecalMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (UMaterialInterface* Existing = Cached.Get()) { return Existing; }
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineMaterials/DefaultDeferredDecalMaterial"));
		if (Mat) { Cached = Mat; }
		return Mat;
	}
}

UMaterialInterface* ASeinDecalFormationPreviewActor::ResolvePreviewMaterial() const
{
	return PreviewMaterial ? ToRawPtr(PreviewMaterial) : SeinDecalPreviewLocal::GetFallbackDecalMaterial();
}

void ASeinDecalFormationPreviewActor::EnsureElementCount_Implementation(int32 Count)
{
	UMaterialInterface* SourceMat = ResolvePreviewMaterial();

	while (Decals.Num() < Count)
	{
		const int32 NewIndex = Decals.Num();
		const FName CompName = *FString::Printf(TEXT("PreviewDecal_%d"), NewIndex);
		UDecalComponent* Decal = NewObject<UDecalComponent>(this, CompName);
		if (!Decal)
		{
			UE_LOG(LogSeinDecalPreview, Warning,
				TEXT("EnsureElementCount: failed to allocate decal component at index %d"), NewIndex);
			break;
		}

		Decal->SetupAttachment(GetRootComponent());
		Decal->SetMobility(EComponentMobility::Movable);
		Decal->SetVisibility(false);     // start hidden — SetPositions shows
		// Constant top-down projection rotation: the component's -90° pitch maps its local X
		// (projection direction) to world -Z. Set once at create time.
		Decal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));

		// One MID per decal so each holds its own tint.
		UMaterialInstanceDynamic* MID = SourceMat ? UMaterialInstanceDynamic::Create(SourceMat, this) : nullptr;
		if (MID) { Decal->SetDecalMaterial(MID); }

		Decal->RegisterComponent();

		Decals.Add(Decal);
		DecalMIDs.SetNum(Decals.Num());
		DecalMIDs[NewIndex] = MID;
	}
}

void ASeinDecalFormationPreviewActor::UpdateElement_Implementation(int32 Index, const FVector& WorldPos, const FLinearColor& Tint, float RadiusUU)
{
	if (!Decals.IsValidIndex(Index)) return;
	UDecalComponent* Decal = Decals[Index];
	if (!Decal) return;

	Decal->SetWorldLocation(WorldPos);

	// DecalSize is a component-local half-extent (X = projection depth, Y/Z = the projected
	// plane). The -90° pitch maps X→world Z. Y/Z half-extent = footprint radius → equal Y/Z
	// gives a square footprint spanning the footprint diameter.
	const float GroundHalf = ResolveFootprintSize(RadiusUU) * 0.5f;
	const float DepthHalf  = ProjectionDepthUU * 0.5f;
	const FVector NewSize(DepthHalf, GroundHalf, GroundHalf);
	if (!Decal->DecalSize.Equals(NewSize))
	{
		// DecalSize is captured by the render proxy; a runtime change needs an explicit
		// render-state refresh or the decal keeps its creation size.
		Decal->DecalSize = NewSize;
		if (Decal->IsRegistered()) { Decal->MarkRenderStateDirty(); }
	}

	// Only the quality tint is driven from C++; the ring's shape is owned by the material.
	// Per-member radius is reflected by the decal's projected size (DecalSize above).
	if (UMaterialInstanceDynamic* MID = DecalMIDs.IsValidIndex(Index) ? DecalMIDs[Index] : nullptr)
	{
		MID->SetVectorParameterValue(TintParameterName, Tint);
	}
}

void ASeinDecalFormationPreviewActor::SetElementVisible_Implementation(int32 Index, bool bVisible)
{
	if (Decals.IsValidIndex(Index) && Decals[Index])
	{
		Decals[Index]->SetVisibility(bVisible);
	}
}

int32 ASeinDecalFormationPreviewActor::NumElements_Implementation() const
{
	return Decals.Num();
}
