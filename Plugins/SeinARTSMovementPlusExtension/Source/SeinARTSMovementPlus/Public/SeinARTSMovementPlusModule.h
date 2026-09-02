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
 *          shutdown register this extension's stable simulation-content and
 *          exact movement-state coverage descriptors; the concrete classes
 *          are discovered through the framework-owned USeinMovement root and
 *          selected through FSeinMovementPayload::MovementClass.
 */

#pragma once

#include "Modules/ModuleManager.h"
#include "Serialization/SeinMovementStateCoverage.h"
#include "Serialization/SeinSimulationContentRegistry.h"

class FSeinARTSMovementPlusModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;

	/** False keeps bootstrap fail-closed after a content-registration error. */
	bool IsSimulationContentContributorReady() const
	{
		return SimulationContentRegistrationHandle.IsValid()
			&& StateCoverageHandles.Num() == 5;
	}

private:
	TArray<FSeinMovementStateCoverageRegistrationHandle>
		StateCoverageHandles;
	FSeinSimulationContentRegistrationHandle SimulationContentRegistrationHandle;
};
