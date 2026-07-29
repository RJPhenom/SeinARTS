/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 */

#pragma once

#include "Modules/ModuleManager.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Settings/SeinConfigFingerprintRegistry.h"

class FSeinARTSSquadModule : public IModuleInterface
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
	FSeinConfigFingerprintRegistrationHandle ConfigFingerprintRegistrationHandle;
	FSeinSimulationContentRegistrationHandle SimulationContentRegistrationHandle;
	TArray<FSeinPoolObjectCodecRegistrationHandle>
		PoolObjectCodecHandles;
};
