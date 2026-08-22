/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 *
 * @file         SeinARTSOnlineServicesModule.cpp
 * @author       RJ Macklem
 * @created      21 Aug 2026
 * @latest       21 Aug 2026
 * @brief        Starts and safely unloads the optional online-services module.
 *
 * @disclaimer   This code was generated in whole or in part with the assistance
 *               of an AI language model.
 */

#include "SeinARTSOnlineServicesModule.h"

#include "Subsystem/SeinOnlineServicesSubsystem.h"
#include "UObject/UObjectIterator.h"

IMPLEMENT_MODULE(FSeinARTSOnlineServicesModule, SeinARTSOnlineServices)

DEFINE_LOG_CATEGORY_STATIC(LogSeinARTSOnlineServicesModule, Log, All);

void FSeinARTSOnlineServicesModule::StartupModule()
{
	UE_LOG(LogSeinARTSOnlineServicesModule, Log,
		TEXT("SeinARTSOnlineServices module started."));
}

void FSeinARTSOnlineServicesModule::PreUnloadCallback()
{
	check(IsInGameThread());
	for (TObjectIterator<USeinOnlineServicesSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
}

void FSeinARTSOnlineServicesModule::ShutdownModule()
{
	UE_LOG(LogSeinARTSOnlineServicesModule, Log,
		TEXT("SeinARTSOnlineServices module shut down."));
}
