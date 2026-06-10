/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSLevelDataLog.h
 * @brief   Module-shared log categories for SeinARTSLevelData.
 *
 *          Declared extern here + defined ONCE in SeinARTSLevelDataModule.cpp
 *          (the SeinARTSCoreEntityLog.h / LogSeinNet pattern) so they register at
 *          module load and ALWAYS appear in the Output Log's category filter.
 *          File-local DEFINE_LOG_CATEGORY_STATIC categories only surface lazily
 *          (after first emit) and don't reliably show in the filter dropdown.
 *          Do NOT re-introduce any of these via _STATIC in a single file: a
 *          shared-name static category collides under adaptive-unity builds (C2011).
 */

#pragma once

#include "CoreMinimal.h"

/** Unified level-data substrate: bake orchestration + runtime load (USeinLevelDataDefault). */
SEINARTSLEVELDATA_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinLevelData, Log, All);

/** Per-world substrate owner subsystem: instantiate / load / bake drive (USeinLevelDataSubsystem). */
SEINARTSLEVELDATA_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinLevelDataSubsystem, Log, All);
