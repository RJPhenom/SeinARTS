/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNavigationLog.h
 * @brief   Module-shared log categories for SeinARTSNavigation.
 *
 *          Declared extern here + defined ONCE in SeinARTSNavigationModule.cpp
 *          (the SeinARTSCoreEntityLog.h / LogSeinNet pattern) so they register at
 *          module load and ALWAYS appear in the Output Log's category filter.
 *          File-local DEFINE_LOG_CATEGORY_STATIC categories only surface lazily
 *          (after first emit) and don't reliably show in the filter dropdown.
 *          Do NOT re-introduce any of these via _STATIC in a single file: a
 *          shared-name static category collides under adaptive-unity builds (C2011).
 */

#pragma once

#include "CoreMinimal.h"

/** Active-nav lifecycle / runtime load / path-request budget (USeinNavigationSubsystem). */
SEINARTSNAVIGATION_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinNavSubsystem, Log, All);

/** Default A* nav: bake, pathfinding, runtime grid load (USeinNavigationAStar). */
SEINARTSNAVIGATION_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinNavigationAStar, Log, All);

/** Nav debug scene-proxy cell viz (USeinNavDebugComponent). */
SEINARTSNAVIGATION_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinNavDebug, Log, All);

/** Per-tick dynamic-blocker stamping pipeline (FSeinNavBlockerStampSystem). */
SEINARTSNAVIGATION_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinNavBlockerStamp, Log, All);
