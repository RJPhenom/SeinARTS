#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FSeinARTS_Framework_Editor : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
