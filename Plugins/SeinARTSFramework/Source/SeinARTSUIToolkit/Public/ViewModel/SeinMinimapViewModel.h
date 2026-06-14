/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMinimapViewModel.h
 * @brief   Read-only data feed for the minimap. Owned by USeinUISubsystem and refreshed
 *          each sim tick. Derives the play-area bounds from the level-data substrate,
 *          enumerates live entities into a compact blip list (fog-culling enemies),
 *          resolves the background texture (designer override → baked), and rebuilds a
 *          small fog overlay texture at a low cadence. The widget renders from this.
 */

#pragma once

#include "CoreMinimal.h"
#include "Data/SeinMinimapTypes.h"
#include "SeinMinimapViewModel.generated.h"

class USeinWorldSubsystem;
class UTexture2D;

/** Broadcast after Refresh() rebuilds the blip list (UMG can re-paint on this). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMinimapViewModelRefreshed);

/**
 * View-model for the minimap. Style-free: blips carry relation + size class, and the
 * widget maps those to color/radius. Positions are north-up normalized [0,1] map space.
 */
UCLASS(BlueprintType)
class SEINARTSUITOOLKIT_API USeinMinimapViewModel : public UObject
{
	GENERATED_BODY()

public:
	/** Bind to the sim subsystem + owning world. Safe to call with a null subsystem
	 *  (the PC / level data may not exist yet — Refresh re-resolves lazily). */
	void Initialize(USeinWorldSubsystem* InWorldSubsystem, UWorld* InWorld);

	/** Rebuild cached minimap data from the sim. Called each sim tick by USeinUISubsystem. */
	void Refresh();

	// ========== Exposed data (read by the widget) ==========

	/** World-space XY of the play-area min corner (matches the baked grid origin). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	FVector2D WorldBoundsMin = FVector2D::ZeroVector;

	/** World-space XY of the play-area max corner (origin + dims * cell size). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	FVector2D WorldBoundsMax = FVector2D(1.0, 1.0);

	/** Ground-plane Z used for screen<->world deprojection (the grid origin Z). */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	float GroundZ = 0.0f;

	/** True once the play-area bounds have been resolved from the level-data substrate. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	bool bHasBounds = false;

	/** Live unit markers, rebuilt every refresh. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	TArray<FSeinMinimapBlip> Blips;

	/** Fog overlay texture (alpha hides terrain): visible = transparent, explored =
	 *  dim, unexplored = opaque. Null when the world has no active fog. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	TObjectPtr<UTexture2D> FogTexture;

	/** Background terrain texture (per-level override, else baked). Null if neither exists. */
	UPROPERTY(BlueprintReadOnly, Category = "SeinARTS|UI|Minimap")
	TObjectPtr<UTexture2D> BackgroundTexture;

	/** Fired after each Refresh(). */
	UPROPERTY(BlueprintAssignable, Category = "SeinARTS|UI|Minimap")
	FOnMinimapViewModelRefreshed OnRefreshed;

	// ========== Config ==========

	/** Square edge of the fog overlay texture in texels. Higher = sharper fog edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap", meta = (ClampMin = "16", ClampMax = "512"))
	int32 FogTextureResolution = 256;

	/** Rebuild the fog overlay every N refreshes (sim ticks). 1 = every tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap", meta = (ClampMin = "1"))
	int32 FogUpdateInterval = 4;

	/** Box-blur radius (texels) applied to the fog overlay each rebuild to soften the hard
	 *  per-cell edges. 0 = off. Works together with the texture's bilinear filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap", meta = (ClampMin = "0", ClampMax = "8"))
	int32 FogBlurRadius = 0;

	/** Fog overlay color for explored-but-not-currently-visible cells (alpha darkens terrain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap")
	FColor FogExploredColor = FColor(0, 0, 0, 120);

	/** Fog overlay color for never-explored cells (alpha near-opaque hides terrain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SeinARTS|UI|Minimap")
	FColor FogUnexploredColor = FColor(2, 2, 4, 255);

	// All five fog tunables above are SEEDED from USeinARTSUISettings on Initialize; the
	// defaults here are just fallbacks. Widgets may override any of them at runtime.

private:
	void ResolveBounds();
	void ResolveBackground();
	void RebuildBlips();
	void UpdateFogTexture();

	UPROPERTY()
	TWeakObjectPtr<USeinWorldSubsystem> WorldSubsystem;

	UPROPERTY()
	TWeakObjectPtr<UWorld> WorldPtr;

	int32 RefreshCounter = 0;
};
