/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinNavigationAStarAsset.h
 * @brief   Baked grid data for USeinNavigationAStar — single-layer 2D cell
 *          walkability + optional cost. Serialized to disk by the bake pipeline
 *          and loaded at level begin-play.
 */

#pragma once

#include "CoreMinimal.h"
#include "SeinNavigationAsset.h"
#include "Types/FixedPoint.h"
#include "Types/Vector.h"
#include "SeinNavigationAStarAsset.generated.h"

/** Per-cell baked data. */
USTRUCT()
struct FSeinAStarCell
{
	GENERATED_BODY()

	/** Non-zero = passable, 0 = blocked. Cost values > 1 slow traversal
	 *  (multiplied against unit step cost during A*). 255 = impassable. */
	UPROPERTY()
	uint8 Cost = 1;

	/** Per-direction connectivity bitmask. Bit N is set iff a unit can traverse
	 *  from this cell to the neighbor in direction N. Direction indices match
	 *  NeighborDX/DY in the A* search: 0..3 cardinal (E/W/N/S), 4..7 diagonal
	 *  (NE/SE/NW/SW). Baked by a per-edge midpoint trace that checks slope on
	 *  both halves + max-step-height. Replaces live slope math in A*. */
	UPROPERTY()
	uint8 Connections = 0;

	/** World-space Z at the cell's center (for visual placement + debug). */
	UPROPERTY()
	FFixedPoint Height = FFixedPoint::Zero;
};

UCLASS(BlueprintType, meta = (DisplayName = "Sein Nav A* Asset"))
class SEINARTSNAVIGATION_API USeinNavigationAStarAsset : public USeinNavigationAsset
{
	GENERATED_BODY()

public:

	/** Grid width in cells (X axis). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|A* Grid")
	int32 Width = 0;

	/** Grid height in cells (Y axis). */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|A* Grid")
	int32 Height = 0;

	/** World units per cell edge. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|A* Grid")
	FFixedPoint CellSize = FFixedPoint::FromInt(100);

	/** World-space XY of cell (0,0)'s bottom-left corner. */
	UPROPERTY(VisibleAnywhere, Category = "SeinARTS|Navigation|A* Grid")
	FFixedVector Origin = FFixedVector::ZeroVector;

	// Per-cell baked data as parallel flat arrays (struct-of-arrays), size =
	// Width * Height, row-major. Stored this way (instead of a TArray<FSeinAStarCell>)
	// because a TArray<FStruct> can NEVER bulk-serialize — UE writes a full property
	// tag per cell, which inflated this asset ~15x (≈150 bytes/cell on disk for ~10
	// bytes of payload). uint8 / int64 inner types DO bulk-serialize (one memcpy).
	// FSeinAStarCell remains the bake's internal working type only; the runtime grid
	// is reconstructed from these in ApplyAssetData.

	/** Per-cell cost: 0 = blocked, 1..254 = passable (cost multiplier), 255 = impassable. */
	UPROPERTY()
	TArray<uint8> CellCost;

	/** Per-cell 8-direction connectivity bitmask (see FSeinAStarCell::Connections). */
	UPROPERTY()
	TArray<uint8> CellConnections;

	/** Per-cell center height as the raw FFixedPoint (32.32) value. Reconstructed via
	 *  FFixedPoint(int64) on load. int64 bulk-serializes; FFixedPoint (a struct) would not. */
	UPROPERTY()
	TArray<int64> CellHeightRaw;

	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }
};
