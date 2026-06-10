/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSFogOfWarLog.h
 * @brief   Module-shared log categories for SeinARTSFogOfWar.
 *
 *          Declared extern here + defined ONCE in SeinARTSFogOfWarModule.cpp
 *          (the SeinARTSCoreEntityLog.h / SeinARTSNavigationLog.h pattern) so they
 *          register at module load and ALWAYS appear in the Output Log's category
 *          filter. File-local DEFINE_LOG_CATEGORY_STATIC categories only surface
 *          lazily (after first emit) and don't reliably show in the filter dropdown.
 *          Do NOT re-introduce any of these via _STATIC in a single file: a
 *          shared-name static category collides under adaptive-unity builds (C2011).
 */

#pragma once

#include "CoreMinimal.h"

/** Default fog impl: bake (legacy + unified layer), stamping, runtime grid load (USeinFogOfWarDefault). */
SEINARTSFOGOFWAR_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinFogOfWar, Log, All);

/** Active-fog lifecycle / runtime load path (USeinFogOfWarSubsystem). */
SEINARTSFOGOFWAR_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinFogOfWarSubsystem, Log, All);

/** Fog debug scene-proxy cell viz (USeinFogOfWarDebugComponent). */
SEINARTSFOGOFWAR_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinFogOfWarDebug, Log, All);
