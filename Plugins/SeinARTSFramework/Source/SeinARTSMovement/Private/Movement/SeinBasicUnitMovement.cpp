/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicUnitMovement.cpp
 * @note    The RTS default loop (seek + kinematic arrival + face-velocity) was hoisted
 *          to USeinMovement::BP_Tick_Implementation as the BP-authoring default, with its
 *          two decisions factored into the ComputeDesiredSpeed / ComputeSteer hooks.
 *          USeinBasicUnitMovement now adds no behavior of its own — it inherits that
 *          default. See Movement_Mode_Authoring_Plan.md.
 */

#include "Movement/SeinBasicUnitMovement.h"
