/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFormationPreviewActor.h
 * @brief   Holds the per-member decal components that render the destination
 *          preview decals (CoH-style "where will my squad land if I click here").
 *          One actor instance per local player, lifecycle owned by
 *          USeinFormationPreviewSubsystem.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SeinFormationPreviewActor.generated.h"

class UDecalComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Renders N ground decals at the projected formation positions for the local
 * player's current selection.
 *
 * Subclass in Blueprint to set `DecalMaterial` and `DecalExtent`; the cover
 * module's settings page (`USeinARTSCoverSettings::FormationPreviewActorClass`)
 * picks which subclass the subsystem spawns at session start.
 *
 * Pure presentation — never mutates sim state. The decal pool grows on demand
 * (`EnsureDecalCount`) and shrinks back to a soft cap on subsequent updates.
 * Idle (no selection / targeter active) is signaled via `SetVisibility(false)`,
 * which hides every decal in O(N) without destroying the components.
 */
UCLASS(Blueprintable, NotPlaceable)
class SEINARTSCOVER_API ASeinFormationPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ASeinFormationPreviewActor();

	/** Decal material rendered on each per-member decal. Designers subclass in
	 *  Blueprint and set this in the BP CDO. Phase 1: any neutral ground decal
	 *  with a flat alpha. Phase 2/3: replaced by cover-quality-tinted variants
	 *  (heavy / light / negative) per-cell.
	 *
	 *  Null = no decals will render. The subsystem still updates positions; if
	 *  the designer sets the material later (live edit), subsequent SetPositions
	 *  calls will produce visible decals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	TObjectPtr<UMaterialInterface> DecalMaterial = nullptr;

	/** Full size of the decal's ground footprint in world units (square). Set to
	 *  the diameter you want — e.g. 128 cm for a roughly squad-member-sized decal.
	 *  The material draws whatever shape it wants inside this square (circle,
	 *  hexagon, etc.); larger values just give the material more room to draw in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (ClampMin = "1.0", UIMin = "32.0", UIMax = "512.0"))
	float GroundSizeUU = 128.f;

	/** Total projection depth in world units — how far the decal box extends
	 *  vertically (centered on the placement position, so it reaches half this
	 *  amount above and below). Should be small (~64) so the decal hits ground
	 *  geometry only and doesn't bleed onto ceilings / overhead structures. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS",
		meta = (ClampMin = "1.0", UIMin = "16.0", UIMax = "256.0"))
	float ProjectionDepthUU = 64.f;

	/** Vertical offset added to each position when transforming the decal —
	 *  pushes decals slightly above the projected ground point so z-fighting
	 *  with terrain materials is avoided. Tune per-project. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS")
	float ZOffsetUU = 4.f;

	/** Material vector parameter that receives the per-decal cover tint. The
	 *  designer's decal material should expose a Vector Parameter named this
	 *  and use it as a multiplier on the base color (or alpha-weighted layer).
	 *  Unused if the material has no parameter by this name — set call is safe
	 *  on MIDs (engine logs a warning and skips). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover")
	FName TintParameterName = TEXT("Tint");

	/** Tint applied to decals at cells where there is NO cover at all (or
	 *  where the cell's best cover quality tag isn't present in
	 *  `CoverQualityTints`). White (1,1,1,1) is a no-op multiplier — the
	 *  material's intrinsic color shows through unmodified. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover")
	FLinearColor NoCoverTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** Cover-quality tag → tint color. Subsystem queries the best cover tag
	 *  at each formation cell and looks the result up here for the decal
	 *  tint. Missing tags fall back to `NoCoverTint`. Designers can edit the
	 *  default colors per-BP-subclass or add entries for project-specific
	 *  cover tags. Default mapping:
	 *    SeinARTS.Cover.Heavy    → green
	 *    SeinARTS.Cover.Light    → yellow
	 *    SeinARTS.Cover.Negative → red
	 *  (Designer-extensible — register additional tags in their project tags
	 *  list and add corresponding entries here.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|Cover",
		meta = (ForceInlineRow))
	TMap<FGameplayTag, FLinearColor> CoverQualityTints;

	// Public API
	// ====================================================================================================

	/** Place N decals at the given world positions and show them. Lazily
	 *  grows the decal pool to fit. Decals at indices >= Positions.Num() are
	 *  hidden in place (not destroyed) — keeps them ready for subsequent
	 *  larger selections without churn. Material is applied each call so live
	 *  edits to `DecalMaterial` take effect on next update.
	 *
	 *  `CoverQualities` is an optional parallel array of per-position best-
	 *  cover quality tags (one tag per position) for color-coding. Pass an
	 *  empty array to skip cover tinting (decals render with `NoCoverTint`).
	 *  When non-empty, length should match WorldPositions; mismatched entries
	 *  fall back to NoCoverTint. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Cover|Preview")
	void SetPositions(const TArray<FVector>& WorldPositions, const TArray<FGameplayTag>& CoverQualities);

	/** Hide every decal in place without destroying components. Used when the
	 *  selection clears, when the targeter activates, or when the cursor moves
	 *  off the world. */
	UFUNCTION(BlueprintCallable, Category = "SeinARTS|Cover|Preview")
	void HideAll();

protected:
	/** Decal pool — grows up to the largest formation seen this session,
	 *  reused across selection changes. Components past the current usage
	 *  count are SetVisibility(false) rather than destroyed. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> Decals;

	/** Per-decal Material Instance Dynamic, parallel to `Decals`. Allocated
	 *  lazily by `EnsureDecalCount` from the base `DecalMaterial`. We need a
	 *  MID per decal because the tint differs per cell (each cell can be in
	 *  a different cover quality). Reusing one MID across decals would push
	 *  one decal's tint to every decal — visually catastrophic. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DecalMIDs;

	/** Per-decal change guards — parallel arrays to `Decals`, tracking the
	 *  last position and tint pushed to each component. `SetPositions` skips
	 *  the SetWorldLocation / SetVectorParameterValue calls when the value
	 *  matches what was last applied — avoiding the SceneComponent dirty-
	 *  mark + render-thread MID proxy update for unchanged decals.
	 *
	 *  Cursor still hovering on the same spot? Every decal is unchanged,
	 *  every per-frame setter is a no-op cheap compare. The previous
	 *  unconditional setter chain paid hundreds of marked-dirty events per
	 *  second on idle cursor. */
	TArray<FVector>      LastDecalWorldPositions;
	TArray<FLinearColor> LastDecalTints;

	/** Ensure the decal pool has at least Count components. Newly created
	 *  components are attached to RootComponent, set up with DecalMaterial +
	 *  DecalExtent, and start hidden. */
	void EnsureDecalCount(int32 Count);

	/** Apply current DecalMaterial / DecalExtent to a single component (used
	 *  on first creation and on every SetPositions call so live BP-CDO edits
	 *  propagate immediately). Returns (or lazily creates) the MID owned by
	 *  this decal so the caller can set per-decal tint without re-creating
	 *  the MID each frame. */
	UMaterialInstanceDynamic* ApplyDecalSettings(UDecalComponent* Decal, int32 DecalIndex);

	/** Resolve a cover quality tag to its configured tint, with fallback to
	 *  `NoCoverTint` when the tag is invalid or not present in the map. */
	FLinearColor ResolveTintForQuality(const FGameplayTag& QualityTag) const;
};
