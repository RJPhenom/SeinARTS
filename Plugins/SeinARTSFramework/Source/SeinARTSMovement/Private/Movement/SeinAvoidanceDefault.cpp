/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinAvoidanceDefault.cpp
 * @author       RJ Macklem
 * @created      3 Jul 2026
 * @latest       13 Aug 2026
 * @brief        Connects the default avoidance policy to its private tick kernel.
 *
 *               The UObject remains the designer-selected policy surface. The
 *               private kernel owns deterministic tick execution so this seam
 *               stays small and replaceable.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "Movement/SeinAvoidanceDefault.h"

#include "Movement/SeinAvoidanceDefaultKernel.h"

void USeinAvoidanceDefault::ComputeAvoidance(USeinWorldSubsystem& World)
{
	FSeinAvoidanceDefaultKernel(*this).Execute(World);
}
