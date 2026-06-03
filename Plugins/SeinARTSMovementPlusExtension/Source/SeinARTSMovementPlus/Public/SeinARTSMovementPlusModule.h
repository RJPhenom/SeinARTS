/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSMovementPlusModule.h
 * @brief   Module class for the SeinARTSMovementPlus extension module.
 *
 *          This module only HOSTS the concrete movement-class implementations
 *          (Infantry / Wheeled / Tracked / Hover / Flight) and their per-class
 *          tuning data. All shared steering infrastructure, the abstract
 *          USeinMovement base, USeinBasicMovement / USeinBasicUnitMovement, the
 *          move-to action + proxy, the movement BPFL, and the debug-draw show
 *          flags stay in the framework's SeinARTSMovement module. Startup /
 *          shutdown are intentionally empty — the classes are discovered via
 *          reflection and selected through FSeinMovementComponent::MovementClass.
 */

#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSMovementPlusModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
