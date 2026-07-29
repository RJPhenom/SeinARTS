/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSCoverModule.h
 * @brief   Runtime module for the opt-in cover extension. Owns query/system
 *          startup and its lockstep settings-fingerprint contribution; preview
 *          rendering remains in the framework and Cover augments its data.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Serialization/SeinPoolObjectCodecRegistry.h"
#include "Settings/SeinConfigFingerprintRegistry.h"

class FSeinARTSCoverModule : public IModuleInterface
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
	FSeinPoolObjectCodecRegistrationHandle PoolObjectCodecHandle;
};
