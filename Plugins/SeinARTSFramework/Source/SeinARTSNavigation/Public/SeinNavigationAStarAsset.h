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

	/** Flat row-major cell array (size = Width * Height). */
	UPROPERTY()
	TArray<FSeinAStarCell> Cells;

	FORCEINLINE int32 CellIndex(int32 X, int32 Y) const { return Y * Width + X; }
	FORCEINLINE bool IsValidCoord(int32 X, int32 Y) const { return X >= 0 && X < Width && Y >= 0 && Y < Height; }
};
