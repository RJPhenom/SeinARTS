#pragma once

#include "Modules/ModuleManager.h"
#include "Serialization/SeinSimulationContentRegistry.h"
#include "Serialization/SeinCanonicalStateRegistry.h"

class FSeinARTSLevelDataModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void PreUnloadCallback() override;
	virtual void ShutdownModule() override;

	/** False keeps bootstrap fail-closed after a content-registration error. */
	bool IsSimulationContentContributorReady() const
	{
		return SimulationContentRegistrationHandle.IsValid()
			&& CanonicalStateRegistrationHandle.IsValid();
	}

private:
	void ReleaseModuleOwnedState();
	FSeinSimulationContentRegistrationHandle SimulationContentRegistrationHandle;
	FSeinCanonicalStateRegistrationHandle CanonicalStateRegistrationHandle;
};
