/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file:		SeinARTSCoreEntityModule.h
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Module declaration for the SeinARTSCoreEntity module.
 *				Defines the FSeinARTSCoreEntity module class with startup
 *				and shutdown lifecycle hooks for the entity/ECS layer
 *				of the deterministic lockstep simulation framework.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FSeinARTSCoreEntity : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
