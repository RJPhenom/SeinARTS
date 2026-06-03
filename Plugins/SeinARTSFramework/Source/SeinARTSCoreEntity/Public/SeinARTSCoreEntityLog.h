/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoreEntityLog.h
 * @brief   Module-shared log categories for SeinARTSCoreEntity.
 *
 *          Each of these categories is used from MORE THAN ONE .cpp in the
 *          module, so they are declared extern here and defined ONCE in
 *          SeinARTSCoreEntityModule.cpp (the same pattern as LogSeinNet /
 *          LogSeinAI). Do NOT re-introduce any of them via
 *          DEFINE_LOG_CATEGORY_STATIC in an individual file: a file-local
 *          static category with a shared name redefines the category struct the
 *          instant the adaptive unity build packs two such files into one
 *          translation unit (C2011). Categories used in exactly ONE .cpp may
 *          still use DEFINE_LOG_CATEGORY_STATIC.
 */

#pragma once

#include "CoreMinimal.h"

/** Sim / render-bridge general logging (ASeinActor, USeinEntityComponent,
 *  USeinWorldSubsystem). */
SEINARTSCOREENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinSim, Log, All);

/** Actor-bridge spawn/teardown logging (USeinEntityComponent,
 *  USeinActorBridgeSubsystem). */
SEINARTSCOREENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinBridge, Log, All);

/** Shared Blueprint-function-library logging (entity / component / ability /
 *  sim-mutation BPFLs). */
SEINARTSCOREENTITY_API DECLARE_LOG_CATEGORY_EXTERN(LogSeinBPFL, Log, All);
