/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    ElevationMode.h
 * @brief   Shared enum for nav cell-data elevation storage. Lives in SeinARTSCore
 *          so both SeinARTSCoreEntity (plugin settings) and SeinARTSNavigation
 *          (cell-data structs) can reference it without a circular dep.
 */

#pragma once

#include "CoreMinimal.h"
#include "ElevationMode.generated.h"

/**
 * Elevation mode selects which cell-data struct a FSeinNavigationLayer stores.
 *   None         — 4-byte flat cells, no elevation info.
 *   HeightOnly   — 6-byte cells with discrete height tier.
 *   HeightSlope  — 8-byte cells with fine-grained height + quantized slope vector.
 */
UENUM(BlueprintType)
enum class ESeinElevationMode : uint8
{
	None         UMETA(DisplayName = "Flat (4 bytes/cell)"),
	HeightOnly   UMETA(DisplayName = "Height Only (6 bytes/cell)"),
	HeightSlope  UMETA(DisplayName = "Height + Slope (8 bytes/cell)"),
};
