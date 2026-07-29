// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Serialization/SeinSimulationContentRegistry.h"

class FSeinARTSFrameworkModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;

	/** False keeps bootstrap fail-closed after a content-registration error. */
	bool IsSimulationContentContributorReady() const
	{
		return SimulationContentRegistrationHandle.IsValid();
	}

private:
	void ReleaseModuleOwnedState();

	FSeinSimulationContentRegistrationHandle SimulationContentRegistrationHandle;
};
