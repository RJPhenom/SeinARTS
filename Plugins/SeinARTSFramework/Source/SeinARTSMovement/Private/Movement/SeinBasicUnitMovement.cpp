/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicUnitMovement.cpp
 */

#include "Movement/SeinBasicUnitMovement.h"

FSeinMotion USeinBasicUnitMovement::ComputeMotion_Implementation(USeinMoverHandle* Mover)
{
	// Basic Unit = the framework's default ground policy: translate toward the path at terrain-scaled
	// top speed (bent by local avoidance) and face the direction of travel at TurnRate. It adds nothing
	// to the base default; kept as a clear, BP-parentable picker entry for the standard RTS ground feel.
	return Super::ComputeMotion_Implementation(Mover);
}
