/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinMovementTraceLog.h
 * @brief   Shared log category for the movement trace ([EP]/[UNIT]/[ORPHAN] from
 *          FSeinMovementTraceSystem, [ARRIVE] from the harness/action, [THROTTLE]
 *          + [ESC] from the action). One switch lights the whole written picture:
 *          `log LogSeinMoveTrace Verbose`. Defined in SeinMovementSubsystem.cpp.
 *
 *          Grammar note: an [ARRIVE] line immediately followed by an [ESC]
 *          escape-done / escape-overshoot line for the same handle is the
 *          hold-escape ladder's INTERNAL leg arriving — not an order ending.
 *          Every other [ARRIVE] still means the order completed there.
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSeinMoveTrace, Log, All);
