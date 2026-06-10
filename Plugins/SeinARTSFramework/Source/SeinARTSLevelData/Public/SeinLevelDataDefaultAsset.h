/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinLevelDataDefaultAsset.h
 * @brief   Baked asset for the default (unified-grid) level-data substrate (CP1.1).
 *
 *          Channel-extensible format (planning/Decisions.md D15): a shared header
 *          (grid dims / origin / finest cell size) + the shared per-cell ground height
 *          field (traced once — the dedup win) + a per-cell in-play-area mask (the union
 *          of volume brush shapes; D10) + a list of per-layer channel BLOCKS. Each block
 *          is an opaque byte payload produced by a layer provider (nav, FoW, later
 *          terrain-cost) and tagged by layer id + its own resolution. Adding a channel
 *          (terrain-cost CP1.2) or a second height sample per column (multi-level
 *          roadmap) is an additive change — no re-architecture.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinLevelDataAsset.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinLevelDataDefaultAsset.generated.h"

/** Per-cell flag bits for USeinLevelDataDefaultAsset::CellFlags. */
namespace SeinLevelCellFlags
{
	/** Cell center is inside a level-volume brush (in the play area; D10). */
	static constexpr uint8 InBounds   = 1 << 0;
	/** The down-trace found geometry at this cell (it has a surface). */
	static constexpr uint8 HasSurface = 1 << 1;
}

/** One per-layer channel block in the baked level data (the D15 extensible format). */
USTRUCT()
struct FSeinLevelChannelBlock
{
	GENERATED_BODY()

	/** Layer id — matches ISeinLevelLayerProvider::GetLayerId (e.g. "Nav", "FogOfWar"). */
	UPROPERTY()
	FName LayerId;

	/** This layer's cell size as a MULTIPLE of the finest cell size (1 = finest,
	 *  2 = half-resolution, …) so a coarser layer records its own resolution (D13). */
	UPROPERTY()
	int32 CellSizeMultiple = 1;

	/** Opaque serialized channel payload. The provider writes it; the layer's runtime
	 *  consumer reads it. The substrate does not interpret it. */
	UPROPERTY()
	TArray<uint8> Data;
};

UCLASS()
class SEINARTSLEVELDATA_API USeinLevelDataDefaultAsset : public USeinLevelDataAsset
{
	GENERATED_BODY()

public:

	/** Grid dimensions in finest cells (row-major: index = Y * Width + X). */
	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	/** World units per finest grid cell edge. */
	UPROPERTY()
	FFixedPoint CellSize;

	/** World-space XY (and Z) of cell (0,0)'s corner. */
	UPROPERTY()
	FFixedVector Origin;

	/** Shared per-cell ground height (finest res, row-major) — traced once for every
	 *  layer (the dedup win; D13). */
	UPROPERTY()
	TArray<FFixedPoint> SharedHeight;

	/** Per-cell surface normal · Up (the trace-normal up-dot), finest res. The nav
	 *  layer's slope gate reads this (`Dot(Normal, Up) >= cos(maxSlope)`); a free
	 *  byproduct of the same height trace, so storing it costs one scalar/cell and
	 *  keeps nav's bake bit-identical. */
	UPROPERTY()
	TArray<FFixedPoint> SharedNormalZ;

	/** Per-cell flags (finest res, row-major): bit 0 = in-play-area (the cell center is
	 *  inside a level-volume brush; D10), bit 1 = surface-hit (the down-trace found
	 *  geometry). Out-of-bounds + no-surface cells carry height = trace floor. */
	UPROPERTY()
	TArray<uint8> CellFlags;

	/** Per-layer channel blocks (nav, FoW, later terrain-cost). Extensible (D15). */
	UPROPERTY()
	TArray<FSeinLevelChannelBlock> Channels;
};
