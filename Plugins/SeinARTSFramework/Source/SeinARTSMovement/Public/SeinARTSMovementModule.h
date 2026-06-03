/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSMovementModule.h
 * @brief   Module class for SeinARTSMovement.
 *
 *          Owns the steering show flag (`Sein.Show.Steering`) and the
 *          active-move path debug ticker. Movement subclasses gate their
 *          per-unit debug viz (carrot points, path tangent lines) on the
 *          steering show flag via `IsSteeringShowFlagOnForWorld`.
 */

#pragma once

#include "Modules/ModuleManager.h"

class FSeinARTSMovementModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

#if UE_ENABLE_DEBUG_DRAWING
class UWorld;

namespace UE::SeinARTSMovement
{
	/** True iff some viewport rendering `World` currently has the
	 *  SeinSteering custom show flag enabled. Movement subclasses
	 *  gate their per-unit debug viz (carrot points, path tangent
	 *  lines) on this. */
	SEINARTSMOVEMENT_API bool IsSteeringShowFlagOnForWorld(UWorld* World);
}
#endif
