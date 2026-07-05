/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementTraceLog.h
 * @brief   Shared log category for the movement trace ([EP]/[UNIT]/[ORPHAN] from
 *          FSeinMovementTraceSystem, [ARRIVE] from the harness/action, [THROTTLE]
 *          from the action). One switch lights the whole written picture:
 *          `log LogSeinMoveTrace Verbose`. Defined in SeinMovementSubsystem.cpp.
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSeinMoveTrace, Log, All);
