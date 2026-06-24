/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSMovementModule.h
 * @brief   Module class for SeinARTSMovement.
 *
 *          Owns the steering show flag (`Sein.Show.Steering`) and the
 *          active-move path debug ticker. The flag gates movement debug viz
 *          (today: footprint ring, velocity, avoidance steer) via
 *          `IsSteeringShowFlagOnForWorld`.
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
	 *  SeinSteering custom show flag enabled. Movement debug viz
	 *  gates on this. */
	SEINARTSMOVEMENT_API bool IsSteeringShowFlagOnForWorld(UWorld* World);
}
#endif
