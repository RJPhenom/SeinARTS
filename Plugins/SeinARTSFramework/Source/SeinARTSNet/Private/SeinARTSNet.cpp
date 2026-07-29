/**
 * SeinARTS Framework - Copyright (c) 2026 Phenom Studios, Inc.
 * @file    SeinARTSNet.cpp
 */

#include "SeinARTSNet.h"
#include "SeinLobbySubsystem.h"
#include "SeinNetSubsystem.h"
#include "Simulation/SeinWorldSubsystem.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogSeinNet);

void FSeinARTSNetModule::StartupModule()
{
	UE_LOG(LogSeinNet, Log, TEXT("SeinARTSNet module started."));
}

void FSeinARTSNetModule::PreUnloadCallback()
{
	check(IsInGameThread());

	// Lobby owns callbacks that can initiate Net work, so retire it first.
	for (TObjectIterator<USeinLobbySubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
	for (TObjectIterator<USeinWorldSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->TerminateAndReleaseForModuleUnload(
				TEXT("SeinARTSNet"),
				TEXT("lockstep topology, replay, and lobby adapters are unloading"));
		}
	}
	for (TObjectIterator<USeinNetSubsystem> It; It; ++It)
	{
		if (!It->HasAnyFlags(RF_ClassDefaultObject))
		{
			It->ReleaseModuleOwnedStateForModuleUnload();
		}
	}
}

void FSeinARTSNetModule::ShutdownModule()
{
	UE_LOG(LogSeinNet, Log, TEXT("SeinARTSNet module shut down."));
}

IMPLEMENT_MODULE(FSeinARTSNetModule, SeinARTSNet)
