/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinBasicMovement.cpp
 */

#include "Movement/SeinBasicMovement.h"

FSeinMotion USeinBasicMovement::ComputeMotion_Implementation(USeinMoverHandle* Mover)
{
	// Basic = translate only. Identical to the base default ground policy (head to the current
	// waypoint at terrain-scaled top speed, bent by local avoidance) but with NO rotation — the unit
	// slides toward its goal without turning to face it. The base Tick harness owns arrival, the hard
	// nav-collision floor, ground snap, and velocity persistence.
	FSeinMotion Motion = Super::ComputeMotion_Implementation(Mover);
	Motion.bUpdateFacing = false;
	return Motion;
}
