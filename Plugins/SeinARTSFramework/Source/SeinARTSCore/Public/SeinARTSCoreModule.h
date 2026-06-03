/**
 * SeinARTS Framework 
 * Copyright (c) 2026 Phenom Studios, Inc.
 * 
 * @file:		SeinARTSCoreModule.h
 * @date:		4/3/2026
 * @author:		RJ Macklem
 * @brief:		Module header for the SeinARTSCore module, providing the
 * 				FSeinARTSCore module class declaration. This module serves
 * 				as the foundation layer containing all deterministic
 * 				fixed-point math primitives, geometric types, and core
 * 				utilities required by the lockstep simulation framework.
 * @disclaimer: This code was generated in part by an AI language model.
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FSeinARTSCore : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
