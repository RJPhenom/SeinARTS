/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewActor.cpp
 * @brief   Base destination-preview decal renderer (ported from the Cover
 *          extension; cover-specific tag seeding removed — the tint map is now a
 *          generic, project-configured FGameplayTag→color table).
 */

#include "Preview/SeinFormationPreviewActor.h"

#include "Components/DecalComponent.h"
#include "Components/SceneComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogSeinFormationPreview, Log, All);

namespace SeinFormationPreviewLocal
{
	/** Resolve a fallback decal material when the designer hasn't subclassed +
	 *  set a custom DecalMaterial. Returns the engine's DefaultDeferredDecalMaterial
	 *  (a flat gray decal shipped with every UE build) so the preview renders
	 *  *something* out of the box. The fallback has no Tint parameter, so quality
	 *  tinting is a no-op while it's in use. Cached after first load. */
	static UMaterialInterface* GetFallbackDecalMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> CachedFallback;
		if (UMaterialInterface* Existing = CachedFallback.Get())
		{
			return Existing;
		}
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

	// CoverQualityTints intentionally ships EMPTY in the base framework — the base
	// has no quality vocabulary of its own. A project's preview BP (or the Cover
	// extension's configured BP subclass) populates it for whatever quality tags it
	// uses; the existing SFP_FormationPreview BP carries its cover colours baked in.
}

void ASeinFormationPreviewActor::SetPositions(const TArray<FVector>& WorldPositions, const TArray<FGameplayTag>& CoverQualities)
{
	const int32 Count = WorldPositions.Num();
	EnsureDecalCount(Count);

	// Keep change-guard arrays sized to the decal pool. New entries default to
	// sentinels so the first call after growth always pushes.
	if (LastDecalWorldPositions.Num() < Decals.Num())
	{
		LastDecalWorldPositions.SetNum(Decals.Num());
	}
	if (LastDecalTints.Num() < Decals.Num())
	{
		LastDecalTints.SetNum(Decals.Num());
	}

	// Decals 0..Count-1 → place + show. Decals >=Count → hide. (Rotation is set
	// once at create time in EnsureDecalCount — never per-frame.)
	for (int32 i = 0; i < Decals.Num(); ++i)
	{
		UDecalComponent* Decal = Decals[i];
		if (!Decal) continue;

		if (i < Count)
		{
			// Push current settings each update so live BP-CDO edits propagate.
			// Returns the per-decal MID for the tint poke.
			UMaterialInstanceDynamic* MID = ApplyDecalSettings(Decal, i);

			// Per-decal quality tint, guarded against unchanged values (each
			// SetVectorParameterValue dirties the render-thread proxy).
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

			// Guarded position update (SetWorldLocation marks dirty even when
			// unchanged; the cheap compare avoids it on an idle cursor).
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
		TEXT("SetPositions: showing %d decals (pool size %d, %d quality tags)"),
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
		// Constant top-down projection rotation, set once at create time.
		Decal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		Decals.Add(Decal);
		// Keep DecalMIDs index-aligned; ApplyDecalSettings populates on first use.
		DecalMIDs.SetNum(Decals.Num());
		ApplyDecalSettings(Decal, Decals.Num() - 1);
		Decal->RegisterComponent();
	}
}

UMaterialInstanceDynamic* ASeinFormationPreviewActor::ApplyDecalSettings(UDecalComponent* Decal, int32 DecalIndex)
{
	if (!Decal) return nullptr;

	if (DecalMIDs.Num() < Decals.Num())
	{
		DecalMIDs.SetNum(Decals.Num());
	}

	// Designer-set DecalMaterial wins; else the engine fallback so decals render
	// something out of the box. Explicit ToRawPtr to disambiguate the ternary.
	UMaterialInterface* SourceMaterial = DecalMaterial
		? ToRawPtr(DecalMaterial)
		: SeinFormationPreviewLocal::GetFallbackDecalMaterial();

	if (SourceMaterial)
	{
		// One MID per decal so each holds its own tint. Recreated only if the
		// source material changes.
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
		if (MID)
		{
			Decal->SetDecalMaterial(MID);
		}
	}

	// DecalSize is component-local half-extent (X = projection direction, Y/Z =
	// projected plane). The component's -90° pitch maps X→world Z (depth), Y/Z→
	// ground. Equal Y/Z = square footprint.
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
