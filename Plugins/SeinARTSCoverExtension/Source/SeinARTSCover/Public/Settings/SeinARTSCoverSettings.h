/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverSettings.h
 * @brief   Settings for the opt-in SeinARTS Cover Extension. Separate
 *          UDeveloperSettings page ("SeinARTS Cover Extension") so cover
 *          configuration lives entirely inside the extension plugin — the
 *          base framework's USeinARTSCoreSettings carries no cover fields,
 *          keeping the extension fully strippable.
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPath.h"
#include "GameplayTagContainer.h"
#include "Types/FixedPoint.h"
#include "SeinARTSCoverSettings.generated.h"

/**
 * Settings for the SeinARTS Cover Extension. Configure under
 * Project Settings > Plugins > SeinARTS Cover Extension.
 *
 * Lives in the SeinARTSCover module (not the base framework) so the cover
 * system's configuration surface is owned by the extension. When the Cover
 * Extension is not installed, this page simply doesn't exist — no orphaned
 * cover fields linger in the base SeinARTS settings.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "SeinARTS Cover Extension"))
class SEINARTSCOVER_API USeinARTSCoverSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USeinARTSCoverSettings();

	virtual void PostInitProperties() override;

	/**
	 * Active cover-system implementation. The extension's preview decals, BPFL
	 * queries, and cover-aware formation snapping all route through this class.
	 *
	 * Ships with `USeinCoverDefault` as the default: flat-list provider
	 * registry + per-query slot-radius / area-shape test. Game teams can
	 * subclass or replace entirely with a spatial-indexed impl when scale
	 * demands it. Empty path falls back to `USeinCoverDefault::StaticClass()`.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cover System",
		meta = (DisplayName = "Cover System Class",
				MetaClass = "/Script/SeinARTSCover.SeinCoverSystem"))
	FSoftClassPath CoverSystemClass;

	// NOTE: the destination preview is a base-framework feature. Units opt in
	// render-side via a Navigation Renderer (USeinFormationPreviewComponent) on their Blueprints;
	// USeinARTSCoreSettings::FormationPreviewActorClass supplies the default
	// renderer. Cover augments it via the cover-quality hook
	// (USeinWorldSubsystem::PreviewQualityProvider, bound in USeinCoverSubsystem).

	/**
	 * Radius (world units) around the move target within which cover slots are
	 * eligible for snap. Both cover-aware broker resolvers (default + squad
	 * dispatch) read this value at runtime; tune once here rather than opening
	 * two resolver CDOs to keep them in sync.
	 *
	 * Per-member distance check uses this radius — so distant squad members
	 * don't get yanked to slots that aren't really "near them" in formation
	 * space, even when the cursor is in cover. Raise for larger search nets
	 * (more aggressive snapping); lower to make cover-snap engage only when
	 * cells are practically on top of cover.
	 *
	 * Default 500 (~5m) ≈ "the squad is right on top of cover when clicked".
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cover System",
		meta = (DisplayName = "Cover Snap Radius",
				ClampMin = "0.0", UIMin = "100.0", UIMax = "2000.0"))
	FFixedPoint CoverSnapRadius;

	/**
	 * Terrain-type → cover-quality binding. Maps a base-framework terrain tag (authored
	 * in Project Settings > SeinARTS > Terrain) to a cover quality tag, so terrain itself
	 * confers cover — e.g. `SeinARTS.Terrain.Road` → `SeinARTS.Cover.Negative` makes
	 * exposed roads take more damage. Read at query time by `USeinCoverDefault::QueryCoverAt`
	 * (it samples the baked per-cell terrain type under the query point); the resulting
	 * cover is OMNIDIRECTIONAL and NOT fog-gated (terrain isn't hidden information).
	 *
	 * This is the ONLY place terrain↔cover is bound — the base framework knows nothing
	 * about cover. Empty (default) = terrain confers no cover (pure opt-in). A unit behind
	 * stronger entity cover (sandbag = Heavy) still gets that under the canonical
	 * best-quality priority; the terrain cover only wins when it's the strongest present.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Cover System",
		meta = (DisplayName = "Terrain Cover Quality",
				ForceInlineRow))
	TMap<FGameplayTag, FGameplayTag> TerrainCoverQuality;

	// UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
#endif
};
