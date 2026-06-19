/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewActor.h
 * @brief   Per-member ground-decal renderer for the destination preview
 *          ("where will my selection land if I order here"). One instance per
 *          local player, lifecycle owned by USeinFormationPreviewSubsystem.
 *
 *          BASE feature (ported from the Cover extension): the framework owns
 *          the destination preview; Cover/Squad augment it (Cover supplies per-
 *          cell quality tags via USeinWorldSubsystem::PreviewQualityProvider).
 *          Pure presentation — never mutates sim state. Subclass in Blueprint to
 *          author the look (DecalMaterial, tints, or override the styling hook);
 *          USeinARTSCoreSettings::FormationPreviewActorClass picks the subclass.
 *
 *          The quality-tint map is a generic FGameplayTag→color table — the
 *          framework ships it EMPTY (neutral preview); a project (or the Cover
 *          extension's preview BP) populates it for whatever quality vocabulary
 *          it uses. (Property names retain the historic "Cover" spelling so the
 *          existing preview BP's authored values survive the module move.)
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SeinFormationPreviewActor.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable, NotPlaceable)
class SEINARTSFRAMEWORK_API ASeinFormationPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ASeinFormationPreviewActor();

	/** Decal material rendered on each per-member decal. Designers subclass in
	 *  Blueprint and set this in the BP CDO. Null = fall back to the engine's
	 *  stock deferred-decal material so something renders out of the box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	TObjectPtr<UMaterialInterface> DecalMaterial = nullptr;

	/** Full size of the decal's ground footprint in world units (square). Set to
	 *  the diameter you want — e.g. 128 cm for a roughly unit-sized decal. The
	 *  material draws whatever shape it wants inside this square. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (ClampMin = "1.0", UIMin = "32.0", UIMax = "512.0"))
	float GroundSizeUU = 128.f;

	/** Total projection depth in world units — how far the decal box extends
	 *  vertically (centered on the placement position). Keep small (~64) so the
	 *  decal hits ground geometry only and doesn't bleed onto overhead structures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (ClampMin = "1.0", UIMin = "16.0", UIMax = "256.0"))
	float ProjectionDepthUU = 64.f;

	/** Vertical offset added to each position when transforming the decal —
	 *  pushes decals slightly above the projected ground point to avoid
	 *  z-fighting with terrain materials. Tune per-project. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	float ZOffsetUU = 4.f;

	/** Material vector parameter that receives the per-decal quality tint. The
	 *  designer's decal material should expose a Vector Parameter named this and
	 *  use it as a color multiplier. Safe no-op if the material has no such
	 *  parameter (engine logs once and skips). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Quality")
	FName TintParameterName = TEXT("Tint");

	/** Tint applied where there is no quality tag for the cell (or the tag isn't
	 *  in CoverQualityTints). White (1,1,1,1) is a no-op multiplier — the
	 *  material's intrinsic color shows through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Quality")
	FLinearColor NoCoverTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** Quality tag → tint color. The preview subsystem fills a per-position quality
	 *  tag (via USeinWorldSubsystem::PreviewQualityProvider — e.g. the Cover
	 *  extension supplies cover quality) and this maps it to a decal tint. Missing
	 *  tags fall back to NoCoverTint. SHIPS EMPTY in the framework (neutral preview);
	 *  a project's preview BP populates it (e.g. cover Heavy→green / Light→yellow /
	 *  Negative→red). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Quality",
		meta = (ForceInlineRow))
	TMap<FGameplayTag, FLinearColor> CoverQualityTints;

	// Public API
	// ====================================================================================================

	/** Place N decals at the given world positions and show them. Lazily grows the
	 *  decal pool to fit; decals past Positions.Num() are hidden in place (not
	 *  destroyed). Material is applied each call so live BP-CDO edits take effect.
	 *
	 *  `Qualities` is an optional parallel array of per-position quality tags for
	 *  color-coding. Empty = no tinting (NoCoverTint). Length should match
	 *  WorldPositions; mismatched entries fall back to NoCoverTint. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Preview")
	void SetPositions(const TArray<FVector>& WorldPositions, const TArray<FGameplayTag>& CoverQualities);

	/** Hide every decal in place without destroying components. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Preview")
	void HideAll();

protected:
	/** Decal pool — grows up to the largest formation seen this session, reused
	 *  across selection changes. Components past the current usage count are
	 *  SetVisibility(false) rather than destroyed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> Decals;

	/** Per-decal Material Instance Dynamic, parallel to `Decals`. One MID per decal
	 *  because each cell's tint can differ. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DecalMIDs;

	/** Per-decal change guards — skip SetWorldLocation / SetVectorParameterValue
	 *  when the value is unchanged (avoids per-frame render-thread churn on an
	 *  idle cursor). */
	TArray<FVector>      LastDecalWorldPositions;
	TArray<FLinearColor> LastDecalTints;

	/** Ensure the decal pool has at least Count components. */
	void EnsureDecalCount(int32 Count);

	/** Apply current DecalMaterial / size to a single component; returns (or lazily
	 *  creates) the per-decal MID so the caller can set its tint. */
	UMaterialInstanceDynamic* ApplyDecalSettings(UDecalComponent* Decal, int32 DecalIndex);

	/** Resolve a quality tag to its configured tint, falling back to NoCoverTint
	 *  when the tag is invalid or not present in the map. */
	FLinearColor ResolveTintForQuality(const FGameplayTag& QualityTag) const;
};
