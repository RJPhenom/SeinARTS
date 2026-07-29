/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverSquadModule.h
 * @brief   Module declaration for the Cover-Squad bridge module.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"

class FSeinARTSCoverSquadModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;

	/** False keeps bootstrap fail-closed after a content-registration error. */
	bool IsSimulationContentContributorReady() const
	{
		return SimulationContentRegistrationHandle.IsValid();
	}

private:
	FSeinSimulationContentRegistrationHandle SimulationContentRegistrationHandle;
	FSeinPoolObjectCodecRegistrationHandle PoolObjectCodecHandle;
};
