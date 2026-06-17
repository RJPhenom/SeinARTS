/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinTerrainVolume.h
 * @brief   Brush volume that stamps a TERRAIN TYPE onto the cells inside its shape at
 *          bake (the explicit-override authoring source, on top of physical-material
 *          mapping). Drop one, shape its brush, set its TerrainType tag → those cells
 *          carry that type in the baked per-cell terrain channel.
 *
 *          Bake-only: the shared trace reads this volume's brush via EncompassesPoint at
 *          bake time and writes the resolved type index into the asset, so (unlike
 *          ASeinLevelVolume) it needs no runtime bounds snapshot — only the baked output
 *          is consumed, and that is serialized/deterministic. A terrain volume OVERRIDES
 *          the material-derived type for any cell it covers (highest Priority wins on
 *          overlap). Re-bake after moving/editing one.
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "GameplayTagContainer.h"
#include "SeinTerrainVolume.generated.h"

UCLASS(meta = (DisplayName = "Sein Terrain Volume"))
class SEINARTSLEVELDATA_API ASeinTerrainVolume : public AVolume
{
	GENERATED_BODY()

public:
	ASeinTerrainVolume(const FObjectInitializer& ObjectInitializer);

	/** Terrain type this volume stamps onto cells inside its brush at bake. References a
	 *  type authored in `USeinARTSCoreSettings::TerrainTypes` BY TAG (an unset/unknown tag
	 *  resolves to Default = no effect). Cells get that type's nav cost — and, with the
	 *  Cover extension present, its cover interpretation. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Terrain")
	FGameplayTag TerrainType;

	/** Overlap resolution: when a cell sits inside more than one terrain volume, the one
	 *  with the HIGHEST Priority wins (NOT array/iteration order — a small high-priority
	 *  mud patch nested inside a larger low-priority region must win). Ties keep the
	 *  first found; give overlapping regions distinct priorities to be unambiguous. */
	UPROPERTY(EditAnywhere, Category = "SeinARTS|Terrain")
	int32 Priority = 0;

	/** World-space AABB of this volume's brush (a bake-time quick reject before the
	 *  per-cell EncompassesPoint test). */
	FBox GetVolumeWorldBounds() const;
};
