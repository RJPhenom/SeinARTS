/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewActor.cpp
 */

#include "Preview/SeinFormationPreviewActor.h"
#include "Tags/SeinCoverGameplayTags.h"

#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFormationPreview, Log, All);

namespace SeinFormationPreviewLocal
{
	/** Resolve a fallback decal material when the designer hasn't subclassed
	 *  ASeinFormationPreviewActor in BP + set a custom DecalMaterial. Returns
	 *  the engine's DefaultDeferredDecalMaterial (a flat gray decal that's
	 *  always available with the engine content), so the preview decals
	 *  render *something* visible out of the box.
	 *
	 *  The fallback material doesn't expose a Tint vector parameter, so
	 *  cover-quality tinting is a no-op while it's in use — designers wanting
	 *  cover-quality colors must subclass + author a material with a Tint
	 *  parameter (see `TintParameterName`). Cached via TStaticObject so we
	 *  don't pay the load cost on every decal allocation. */
	static UMaterialInterface* GetFallbackDecalMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> CachedFallback;
		if (UMaterialInterface* Existing = CachedFallback.Get())
		{
			return Existing;
		}
		// /Engine/EngineMaterials/DefaultDeferredDecalMaterial is the canonical
		// engine-shipped decal material — it ships with every UE build and
		// covers the "no designer material set" case so the preview is at
		// least visible on a vanilla install. Verified present in UE 5.7
		// engine content. LoadObject is synchronous — fine at first-use,
		// cached thereafter.
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/EngineMaterials/DefaultDeferredDecalMaterial"));
		if (Mat) { CachedFallback = Mat; }
		return Mat;
	}
}

ASeinFormationPreviewActor::ASeinFormationPreviewActor()
{
	// No tick — state is pushed by the subsystem.
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;        // render-side only

	// Bare scene root — decal components attach here.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot"));
	SetRootComponent(Root);

	// Seed the canonical cover-quality → tint mapping. Designers can override
	// in BP subclasses by editing this property; the CDO defaults are the
	// project-neutral choice the user asked for: heavy=green, light=yellow,
	// negative=red (and no-cover=white via NoCoverTint). Lazy init pattern
	// because UE doesn't support TMap literal initialization in UPROPERTY
	// defaults — populate in the constructor so the BP CDO inherits.
	CoverQualityTints.Reset();
	CoverQualityTints.Add(SeinCoverTags::Cover_Heavy,    FLinearColor(0.10f, 0.85f, 0.20f, 1.f));  // green
	CoverQualityTints.Add(SeinCoverTags::Cover_Light,    FLinearColor(0.95f, 0.85f, 0.15f, 1.f));  // yellow
	CoverQualityTints.Add(SeinCoverTags::Cover_Negative, FLinearColor(0.90f, 0.15f, 0.15f, 1.f));  // red
}

void ASeinFormationPreviewActor::SetPositions(const TArray<FVector>& WorldPositions, const TArray<FGameplayTag>& CoverQualities)
{
	const int32 Count = WorldPositions.Num();
	EnsureDecalCount(Count);

	// Keep change-guard arrays sized to the decal pool. Initialize new
	// entries to "impossible" sentinels so the first call after growth
	// always pushes (the guard compares to the sentinel and writes).
	if (LastDecalWorldPositions.Num() < Decals.Num())
	{
		LastDecalWorldPositions.SetNum(Decals.Num());
	}
	if (LastDecalTints.Num() < Decals.Num())
	{
		// FLinearColor default-constructs to (0,0,0,0); first compare against
		// any real tint (including pure black if a designer authors one) will
		// differ from the alpha component, forcing the initial set. Adequate
		// sentinel without an explicit "is initialized" bool.
		LastDecalTints.SetNum(Decals.Num());
	}

	// Rotation is set ONCE at decal-create time in EnsureDecalCount —
	// no per-frame rotation set here. The constant top-down projection
	// (-90, 0, 0) never changes after creation.

	// Decals 0..Count-1 → place at positions, show. Decals >=Count → hide.
	for (int32 i = 0; i < Decals.Num(); ++i)
	{
		UDecalComponent* Decal = Decals[i];
		if (!Decal) continue;

		if (i < Count)
		{
			// Push current settings into the component on every update so live
			// BP-CDO edits to DecalMaterial / DecalExtent / ZOffsetUU propagate
			// immediately. Returns the per-decal MID so we can poke the tint
			// without re-creating it each call.
			UMaterialInstanceDynamic* MID = ApplyDecalSettings(Decal, i);

			// Per-decal cover tint. Length-mismatched CoverQualities (caller
			// passed fewer tags than positions) get NoCoverTint via the
			// IsValidIndex guard; an explicit invalid tag at the position
			// also resolves to NoCoverTint inside ResolveTintForQuality.
			//
			// Guarded: skip SetVectorParameterValue if the tint is unchanged.
			// MID->SetVectorParameterValue marks the material proxy dirty on
			// the render thread regardless of value, so the cheap CPU-side
			// FLinearColor compare avoids a render-thread roundtrip per
			// decal per frame.
			if (MID)
			{
				const FGameplayTag Quality = CoverQualities.IsValidIndex(i)
					? CoverQualities[i] : FGameplayTag();
				const FLinearColor Tint = ResolveTintForQuality(Quality);
				if (Tint != LastDecalTints[i])
				{
					MID->SetVectorParameterValue(TintParameterName, Tint);
					LastDecalTints[i] = Tint;
				}
			}

			// Guarded position update: SetWorldLocation marks the scene-update
			// flag even when called with the same value (UE doesn't compare
			// before marking). The cheap FVector::Equals avoids the dirty-mark
			// for an idle cursor.
			const FVector WorldPos = WorldPositions[i] + FVector(0.f, 0.f, ZOffsetUU);
			if (!WorldPos.Equals(LastDecalWorldPositions[i], 0.01f))
			{
				Decal->SetWorldLocation(WorldPos);
				LastDecalWorldPositions[i] = WorldPos;
			}

			Decal->SetVisibility(true);
		}
		else
		{
			Decal->SetVisibility(false);
		}
	}

	UE_LOG(LogSeinFormationPreview, VeryVerbose,
		TEXT("SetPositions: showing %d decals (pool size %d, %d cover tags)"),
		Count, Decals.Num(), CoverQualities.Num());
}

void ASeinFormationPreviewActor::HideAll()
{
	for (UDecalComponent* Decal : Decals)
	{
		if (Decal) Decal->SetVisibility(false);
	}
}

void ASeinFormationPreviewActor::EnsureDecalCount(int32 Count)
{
	while (Decals.Num() < Count)
	{
		const FName CompName = *FString::Printf(TEXT("PreviewDecal_%d"), Decals.Num());
		UDecalComponent* Decal = NewObject<UDecalComponent>(this, CompName);
		if (!Decal)
		{
			UE_LOG(LogSeinFormationPreview, Warning,
				TEXT("EnsureDecalCount: failed to allocate decal component at index %d"), Decals.Num());
			break;
		}
		Decal->SetupAttachment(GetRootComponent());
		Decal->SetVisibility(false);     // start hidden — SetPositions shows
		Decal->SetMobility(EComponentMobility::Movable);
		// Set the constant top-down projection rotation ONCE at create time;
		// SetPositions no longer touches rotation per-frame (it never changes
		// after creation), saving a SceneComponent dirty-mark per decal per
		// frame on idle cursor.
		Decal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		const int32 NewIdx = Decals.Add(Decal);
		// Keep DecalMIDs index-aligned with Decals — push a null entry that
		// ApplyDecalSettings will populate on first use. Allocating the MID
		// here would force a material assignment before the BP CDO has
		// finished its property init in the editor preview path.
		DecalMIDs.SetNum(Decals.Num());
		(void)NewIdx;
		ApplyDecalSettings(Decal, Decals.Num() - 1);
		Decal->RegisterComponent();
	}
}

UMaterialInstanceDynamic* ASeinFormationPreviewActor::ApplyDecalSettings(UDecalComponent* Decal, int32 DecalIndex)
{
	if (!Decal) return nullptr;

	// Ensure MID array length tracks Decals length — defensive when
	// EnsureDecalCount didn't run on this path (e.g. designer hot-edited
	// the pool mid-session).
	if (DecalMIDs.Num() < Decals.Num())
	{
		DecalMIDs.SetNum(Decals.Num());
	}

	// Resolve the source material to use for this decal. Designer-set
	// DecalMaterial wins; otherwise fall back to the engine's stock
	// DefaultDeferredDecalMaterial so the decals render *something* visible
	// out of the box. Without this fallback, a project with no BP subclass
	// + custom material gets fully-invisible decals — the preview pipeline
	// runs, components attach, positions update, but the decal samples no
	// material and renders nothing. That's the most common "preview broken"
	// failure mode for new projects.
	//
	// Explicit `ToRawPtr()` on the TObjectPtr to silence MSVC's ternary
	// type-deduction ambiguity (TObjectPtr<UMaterialInterface> and
	// UMaterialInterface* are both convertible to either type, so the
	// compiler can't pick a common type without a hint).
	UMaterialInterface* SourceMaterial = DecalMaterial
		? ToRawPtr(DecalMaterial)
		: SeinFormationPreviewLocal::GetFallbackDecalMaterial();

	if (SourceMaterial)
	{
		// Lazy-create a per-decal MID. We need ONE MID PER DECAL so each can
		// hold its own tint vector — reusing a shared MID would push the
		// last-set tint to every decal. Tied to the decal index via the
		// parallel DecalMIDs array; recreated only if the source material
		// changes (designer hot-edits DecalMaterial on the BP CDO, or
		// flips back and forth between custom + fallback).
		UMaterialInstanceDynamic* MID = DecalMIDs.IsValidIndex(DecalIndex) ? DecalMIDs[DecalIndex] : nullptr;
		const bool bMIDStale = !MID || (MID->Parent != SourceMaterial);
		if (bMIDStale)
		{
			MID = UMaterialInstanceDynamic::Create(SourceMaterial, this);
			if (DecalMIDs.IsValidIndex(DecalIndex))
			{
				DecalMIDs[DecalIndex] = MID;
			}
		}

		// SetDecalMaterial accepts a MID directly — it's a UMaterialInterface
		// subclass. This makes the decal sample our MID (with tint param)
		// rather than the static base material. Cheap when value unchanged.
		// Note: the engine fallback material has no `Tint` vector parameter,
		// so SetVectorParameterValue("Tint", ...) becomes a no-op (engine
		// logs once and skips). That's the documented behavior on the
		// TintParameterName UPROPERTY — designers wanting cover-quality
		// tints must author their own material.
		if (MID)
		{
			Decal->SetDecalMaterial(MID);
		}
	}

	// UDecalComponent::DecalSize is component-local half-extent (X = projection
	// direction, Y/Z = projected plane). Our actor rotates the component so its
	// +X points down (FRotator(-90,0,0) in SetPositions), which makes:
	//   - Component X half-extent → world Z half-extent (= projection depth / 2)
	//   - Component Y half-extent → world Y half-extent on ground
	//   - Component Z half-extent → world X half-extent on ground (axis swap from pitch)
	//
	// To present designers with a clean "square decal on ground" knob, we set
	// Y and Z to the same half-extent value derived from GroundSizeUU. Equal Y/Z
	// = square footprint = circular decals when the material draws a circle.
	// X is the projection depth.
	const float GroundHalf = GroundSizeUU * 0.5f;
	const float DepthHalf  = ProjectionDepthUU * 0.5f;
	Decal->DecalSize = FVector(DepthHalf, GroundHalf, GroundHalf);

	return DecalMIDs.IsValidIndex(DecalIndex) ? DecalMIDs[DecalIndex] : nullptr;
}

FLinearColor ASeinFormationPreviewActor::ResolveTintForQuality(const FGameplayTag& QualityTag) const
{
	if (!QualityTag.IsValid()) return NoCoverTint;
	if (const FLinearColor* Found = CoverQualityTints.Find(QualityTag))
	{
		return *Found;
	}
	return NoCoverTint;
}
