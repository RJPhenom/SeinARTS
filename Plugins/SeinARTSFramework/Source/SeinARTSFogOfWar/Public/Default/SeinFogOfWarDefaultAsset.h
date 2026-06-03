/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinFogOfWarDefaultAsset.h
 * @brief   Baked grid data for USeinFogOfWarDefault — single-layer 2D cell
 *          grid with per-cell terrain + static-blocker heights (packed into
 *          bytes for density). Serialized to disk by the bake pipeline and
 *          loaded at level begin-play.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinFogOfWarAsset.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinFogOfWarDefaultAsset.generated.h"

/**
 * Per-cell baked data. Three bytes — height field + static blocker info.
 * Dynamic blockers (smoke, destructibles) are NOT stored here; they live
 * in the runtime impl's dynamic overlay grid.
 */
USTRUCT()
struct FSeinFogOfWarCell
{
	GENERATED_BODY()

	/** Terrain height, quantized: world_z = MinHeight + GroundHeight *
	 *  HeightQuantum. 8-bit gives 256 steps over the map's vertical range. */
	UPROPERTY()
	uint8 GroundHeight = 0;

	/** Height of any static blocker at this cell (sandbags, low wall,
	 *  building), above GroundHeight. 0 = no static blocker. Same quantum
	 *  as GroundHeight. */
	UPROPERTY()
	uint8 BlockerHeight = 0;

	/** Which EVNNNNNN bits this cell's static blocker occludes. Bit 1 = V
	 *  (Normal), bits 2..7 = N0..N5. Relevant only when BlockerHeight > 0. */
	UPROPERTY()
	uint8 BlockerLayerMask = 0;
};

UCLASS(BlueprintType, meta = (DisplayName = "Sein Fog Of War (Default) Asset"))
class SEINARTSFOGOFWAR_API USeinFogOfWarDefaultAsset : public USeinFogOfWarAsset
{
	GENERATED_BODY()

public:

	/** Grid width in cells (X axis). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War|Grid")
	int32 Width = 0;

	/** Grid height in cells (Y axis). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War|Grid")
	int32 Height = 0;

	/** World units per cell edge. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War|Grid")
	FFixedPoint CellSize = FFixedPoint::FromInt(400);

	/** World-space XY of cell (0,0)'s bottom-left corner. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War|Grid")
	FFixedVector Origin = FFixedVector::ZeroVector;

	/** Quantization constants for the per-cell height bytes. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War|Grid")
	FFixedPoint MinHeight = FFixedPoint::Zero;

	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Fog Of War|Grid")
	FFixedPoint HeightQuantum = FFixedPoint::FromInt(10);

	/** Flat row-major cell array (size = Width * Height). */
	UPROPERTY()
	TArray<FSeinFogOfWarCell> Cells;

	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }
};
